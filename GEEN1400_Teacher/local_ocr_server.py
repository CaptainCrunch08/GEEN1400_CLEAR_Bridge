#!/usr/bin/env python3
"""Local, LAN-only handwriting OCR endpoint for the CLEAR teacher ESP32."""

from __future__ import annotations

import argparse
import io
import json
import os
import secrets
import sys
import threading
import time
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import cairosvg
import numpy as np
import torch
from PIL import Image
from transformers import TrOCRProcessor, VisionEncoderDecoderModel

HOST = "0.0.0.0"
PORT = 8765
MAX_BODY = 300_000
MODEL_ID = "microsoft/trocr-base-handwritten"
SVG_NS = "{http://www.w3.org/2000/svg}"
MODEL_LOCK = threading.Lock()
INFERENCE_LOCK = threading.Lock()
JOB_LOCK = threading.Lock()
OCR_MODEL = None
OCR_PROCESSOR = None
JOBS = {}
JOB_TTL_SECONDS = 1800


def load_model():
    global OCR_MODEL, OCR_PROCESSOR
    with MODEL_LOCK:
        if OCR_MODEL is None:
            print("Downloading/loading the local TrOCR model (first run may take several minutes).",
                  flush=True)
            OCR_PROCESSOR = TrOCRProcessor.from_pretrained(MODEL_ID)
            OCR_MODEL = VisionEncoderDecoderModel.from_pretrained(MODEL_ID)
            OCR_MODEL.eval()
            print("Local handwriting model is ready.", flush=True)
    return OCR_PROCESSOR, OCR_MODEL


def parse_svg_board(svg: bytes) -> tuple[str, str, int]:
    if len(svg) > MAX_BODY:
        raise ValueError("drawing_too_large")
    root = ET.fromstring(svg)
    if root.tag != SVG_NS + "svg" or root.attrib.get("viewBox") != "0 0 800 480":
        raise ValueError("unsupported_drawing")
    board = root.attrib.get("data-board", "")
    student = root.attrib.get("data-student", "")
    drawing_id = int(root.attrib.get("data-drawing-id", "0"))
    if board not in ("graph", "whiteboard", "chalkboard") or drawing_id <= 0:
        raise ValueError("invalid_drawing_metadata")
    for element in root.iter():
        if element.tag not in (SVG_NS + "svg", SVG_NS + "rect",
                               SVG_NS + "g", SVG_NS + "line"):
            raise ValueError("unsupported_svg_element")
    return board, student, drawing_id


def image_lines(svg: bytes, board: str):
    png = cairosvg.svg2png(bytestring=svg, output_width=800, output_height=480)
    image = Image.open(io.BytesIO(png)).convert("RGB")
    pixels = np.asarray(image)
    if board == "chalkboard":
        background = np.array([16, 117, 32])
        ink = np.max(np.abs(pixels.astype(np.int16) - background), axis=2) > 42
    else:
        ink = pixels.mean(axis=2) < 150
    ink[:108, :] = False
    ink[420:, :] = False
    row_ink = np.count_nonzero(ink, axis=1)
    x_indices = np.where(ink)[1]
    if not len(x_indices):
        raise ValueError("no_handwriting_found")

    spans = []
    start = -1
    last_ink = -1
    for row in range(108, 421):
        active = row < 420 and row_ink[row] >= 2
        if active:
            if start < 0:
                start = row
            last_ink = row
        elif start >= 0 and row - last_ink > 4:
            if last_ink - start >= 3:
                spans.append((start, last_ink))
            start = -1
    if start >= 0 and last_ink - start >= 3:
        spans.append((start, last_ink))
    if not spans:
        raise ValueError("could_not_separate_handwriting_lines")
    if len(spans) > 40:
        raise ValueError("too_many_handwriting_lines")

    crops = []
    heights = []
    for top, bottom in spans:
        ink_columns = np.where(ink[top:bottom + 1, :])[1]
        if len(ink_columns) == 0:
            continue
        left = max(0, int(ink_columns.min()) - 8)
        right = min(799, int(ink_columns.max()) + 8)
        crop_top = max(0, top - 6)
        crop_bottom = min(479, bottom + 6)
        crop = image.crop((left, crop_top, right + 1, crop_bottom + 1)).convert("L")
        # TrOCR is trained on dark ink on a light background.
        if board == "chalkboard":
            crop = Image.eval(crop, lambda value: 255 - value)
        crops.append(crop.convert("RGB"))
        heights.append(int(bottom - top + 1))
    return crops, heights


def transcribe(svg: bytes) -> str:
    board, _, drawing_id = parse_svg_board(svg)
    crops, heights = image_lines(svg, board)
    processor, model = load_model()
    lines = []
    with INFERENCE_LOCK:
        for index, (crop, _height) in enumerate(zip(crops, heights), start=1):
            print(f"OCR: drawing {drawing_id}, line {index}/{len(crops)}", flush=True)
            inputs = processor(images=crop, return_tensors="pt")
            with torch.inference_mode():
                generated = model.generate(
                    inputs.pixel_values,
                    max_new_tokens=128,
                    num_beams=4,
                )
            text = processor.batch_decode(generated, skip_special_tokens=True)[0].strip()
            if text:
                lines.append(text)
    return "\n".join(lines).encode("utf-8")[:600].decode("utf-8", errors="ignore")


def run_ocr_job(job_id: str, svg: bytes):
    with JOB_LOCK:
        JOBS[job_id]["state"] = "running"
        JOBS[job_id]["updated"] = time.time()
    try:
        text = transcribe(svg)
        if not text:
            raise ValueError("no_text_recognized")
        with JOB_LOCK:
            JOBS[job_id].update(state="complete", result=text, updated=time.time())
    except (ET.ParseError, ValueError) as error:
        with JOB_LOCK:
            JOBS[job_id].update(state="error", error=str(error), updated=time.time())
    except Exception as error:
        print(f"OCR failed: {error}", file=sys.stderr, flush=True)
        with JOB_LOCK:
            JOBS[job_id].update(state="error", error="ocr_failed", updated=time.time())


class Handler(BaseHTTPRequestHandler):
    server_version = "CLEARLocalOCR/1.0"

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args), flush=True)

    def _json(self, status: int, value: dict):
        data = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _text(self, status: int, text: str):
        data = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith("/result/"):
            supplied = self.headers.get("X-CLEAR-Token", "")
            if not secrets.compare_digest(supplied, self.server.ocr_token):
                self._json(401, {"error": "unauthorized"})
                return
            job_id = self.path[len("/result/"):]
            if not job_id or any(ch not in
                                 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                                 for ch in job_id):
                self._json(400, {"error": "invalid_job_id"})
                return
            with JOB_LOCK:
                job = JOBS.get(job_id)
                if job is not None and time.time() - job["updated"] > JOB_TTL_SECONDS:
                    del JOBS[job_id]
                    job = None
                snapshot = dict(job) if job is not None else None
            if snapshot is None:
                self._json(404, {"error": "job_not_found"})
            elif snapshot["state"] in ("pending", "running"):
                self.send_response(202)
                self.send_header("Content-Length", "0")
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
            elif snapshot["state"] == "complete":
                self._text(200, snapshot["result"])
            else:
                self._json(422, {"error": snapshot["error"]})
            return
        if self.path != "/health":
            self._json(404, {"error": "not_found"})
            return
        self._json(200, {
            "service": "CLEAR local OCR",
            "model": MODEL_ID,
            "ready": OCR_MODEL is not None,
        })

    def do_DELETE(self):
        if not self.path.startswith("/result/"):
            self._json(404, {"error": "not_found"})
            return
        supplied = self.headers.get("X-CLEAR-Token", "")
        if not secrets.compare_digest(supplied, self.server.ocr_token):
            self._json(401, {"error": "unauthorized"})
            return
        job_id = self.path[len("/result/"):]
        if not job_id or any(ch not in
                             "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                             for ch in job_id):
            self._json(400, {"error": "invalid_job_id"})
            return
        with JOB_LOCK:
            job = JOBS.get(job_id)
            if job is None:
                status = 404
            elif job["state"] in ("pending", "running"):
                status = 409
            else:
                del JOBS[job_id]
                status = 204
        if status == 204:
            self.send_response(status)
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self._json(status, {"error": "job_not_available"})

    def do_POST(self):
        if self.path != "/recognize":
            self._json(404, {"error": "not_found"})
            return
        expected = self.server.ocr_token
        supplied = self.headers.get("X-CLEAR-Token", "")
        if not secrets.compare_digest(supplied, expected):
            self._json(401, {"error": "unauthorized"})
            return
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "image/svg+xml":
            self._json(415, {"error": "expected_svg"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._json(400, {"error": "invalid_content_length"})
            return
        if length <= 0 or length > MAX_BODY:
            self._json(413, {"error": "drawing_too_large"})
            return
        svg = self.rfile.read(length)
        if len(svg) != length:
            self._json(400, {"error": "incomplete_request"})
            return
        try:
            parse_svg_board(svg)
        except (ET.ParseError, ValueError) as error:
            self._json(422, {"error": str(error)})
            return
        now = time.time()
        job_id = secrets.token_urlsafe(18)
        with JOB_LOCK:
            expired = [key for key, job in JOBS.items()
                       if now - job["updated"] > JOB_TTL_SECONDS]
            for key in expired:
                del JOBS[key]
            if len(JOBS) >= 16:
                self._json(503, {"error": "ocr_queue_full"})
                return
            JOBS[job_id] = {"state": "pending", "updated": now}
        worker = threading.Thread(target=run_ocr_job, args=(job_id, svg), daemon=True)
        worker.start()
        self._text(202, job_id)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--token", default=os.environ.get("CLEAR_OCR_TOKEN", ""),
                        help="shared token (or set CLEAR_OCR_TOKEN)")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    token = args.token.strip()
    if not token:
        token = secrets.token_urlsafe(32)
        print("Generated one-time shared token. Put this in CLEAR_Teacher_Config.h:",
              flush=True)
        print(token, flush=True)
    server = ThreadingHTTPServer((HOST, args.port), Handler)
    server.ocr_token = token
    load_model()
    print(f"CLEAR local OCR listening on http://0.0.0.0:{args.port}", flush=True)
    print("Allow this port on the private network in the computer firewall.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping local OCR.", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
