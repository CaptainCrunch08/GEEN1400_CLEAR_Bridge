// Locks a block of text with the class PIN.
// The teacher week file and the website both use this layout, so a
// copied CLEAR.CSV can be opened on the class page and nowhere else.
//
// CLEARENC1
// salt, base64
// iv, base64
// ciphertext with the 16-byte tag on the end, base64
//
// The key is PBKDF2-HMAC-SHA256, 4096 rounds, 32 bytes.
// Keep this file the same in the student and teacher sketch folders.

#pragma once

#include <Arduino.h>
#include <string.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>

static const unsigned int CLEAR_KDF_ROUNDS = 4096;

// Testing bypass. 1234 is accepted at the class PIN screen.
// It does not replace the saved PIN, and it cannot decrypt a file
// that was sealed with a different PIN.
static const char CLEAR_TEST_PIN[] = "1234";

static bool clearTestPin(const char *pin) {
  return pin != nullptr && strcmp(pin, CLEAR_TEST_PIN) == 0;
}

static bool clearToBase64(const uint8_t *in, size_t len, String &out) {
  if (len == 0) {
    return false;
  }
  size_t room = 4 * ((len + 2) / 3) + 1;
  unsigned char *buf = (unsigned char *)malloc(room);
  if (buf == nullptr) {
    return false;
  }
  size_t wrote = 0;
  int rc = mbedtls_base64_encode(buf, room, &wrote, in, len);
  if (rc != 0) {
    free(buf);
    return false;
  }
  buf[wrote] = 0;
  out = (char *)buf;
  free(buf);
  return true;
}

static bool clearFromBase64(const String &in, uint8_t **out, size_t *outLen) {
  if (in.length() == 0) {
    return false;
  }
  size_t room = (in.length() * 3) / 4 + 4;
  uint8_t *buf = (uint8_t *)malloc(room);
  if (buf == nullptr) {
    return false;
  }
  size_t wrote = 0;
  int rc = mbedtls_base64_decode(buf, room, &wrote,
                                 (const unsigned char *)in.c_str(), in.length());
  if (rc != 0 || wrote == 0) {
    free(buf);
    return false;
  }
  *out = buf;
  *outLen = wrote;
  return true;
}

static bool clearNextLine(const String &text, int &cursor, String &line) {
  if (cursor < 0 || cursor >= text.length()) {
    return false;
  }
  int end = text.indexOf('\n', cursor);
  if (end < 0) {
    end = text.length();
  }
  line = text.substring(cursor, end);
  line.trim();
  cursor = end + 1;
  return line.length() > 0;
}

// salt is random and stored inside the sealed text, so each save stands alone.
static bool clearSealText(const char *pin, const String &plain, String &sealed) {
  if (pin == nullptr || pin[0] == 0 || plain.length() == 0) {
    return false;
  }
  uint8_t salt[16];
  uint8_t iv[12];
  uint8_t key[32];
  esp_fill_random(salt, sizeof(salt));
  esp_fill_random(iv, sizeof(iv));
  int derived = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256, (const unsigned char *)pin, strlen(pin),
    salt, sizeof(salt), CLEAR_KDF_ROUNDS, sizeof(key), key);
  if (derived != 0) {
    return false;
  }
  size_t plainLen = plain.length();
  uint8_t *cipher = (uint8_t *)malloc(plainLen + 16);
  if (cipher == nullptr) {
    memset(key, 0, sizeof(key));
    return false;
  }
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  bool ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0;
  if (ok) {
    ok = mbedtls_gcm_crypt_and_tag(
           &gcm, MBEDTLS_GCM_ENCRYPT, plainLen, iv, sizeof(iv),
           nullptr, 0, (const unsigned char *)plain.c_str(), cipher,
           16, cipher + plainLen) == 0;
  }
  mbedtls_gcm_free(&gcm);
  memset(key, 0, sizeof(key));
  String saltText;
  String ivText;
  String bodyText;
  if (ok) {
    ok = clearToBase64(salt, sizeof(salt), saltText) &&
         clearToBase64(iv, sizeof(iv), ivText) &&
         clearToBase64(cipher, plainLen + 16, bodyText);
  }
  free(cipher);
  if (!ok) {
    return false;
  }
  sealed = "CLEARENC1\n";
  sealed += saltText;
  sealed += "\n";
  sealed += ivText;
  sealed += "\n";
  sealed += bodyText;
  sealed += "\n";
  return true;
}

static bool clearOpenText(const char *pin, const String &sealed, String &plain) {
  if (pin == nullptr || pin[0] == 0) {
    return false;
  }
  int cursor = 0;
  String magic;
  String saltText;
  String ivText;
  String bodyText;
  if (!clearNextLine(sealed, cursor, magic) || magic != "CLEARENC1" ||
      !clearNextLine(sealed, cursor, saltText) ||
      !clearNextLine(sealed, cursor, ivText) ||
      !clearNextLine(sealed, cursor, bodyText)) {
    return false;
  }
  uint8_t *salt = nullptr;
  uint8_t *iv = nullptr;
  uint8_t *body = nullptr;
  size_t saltLen = 0;
  size_t ivLen = 0;
  size_t bodyLen = 0;
  bool parsed = clearFromBase64(saltText, &salt, &saltLen) &&
                clearFromBase64(ivText, &iv, &ivLen) &&
                clearFromBase64(bodyText, &body, &bodyLen);
  if (!parsed || saltLen != 16 || ivLen != 12 || bodyLen <= 16) {
    free(salt);
    free(iv);
    free(body);
    return false;
  }
  uint8_t key[32];
  int derived = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256, (const unsigned char *)pin, strlen(pin),
    salt, saltLen, CLEAR_KDF_ROUNDS, sizeof(key), key);
  size_t plainLen = bodyLen - 16;
  uint8_t *opened = (uint8_t *)malloc(plainLen + 1);
  bool ok = derived == 0 && opened != nullptr;
  if (ok) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    ok = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0;
    if (ok) {
      ok = mbedtls_gcm_auth_decrypt(
             &gcm, plainLen, iv, ivLen, nullptr, 0, body + plainLen, 16,
             body, opened) == 0;
    }
    mbedtls_gcm_free(&gcm);
  }
  memset(key, 0, sizeof(key));
  free(salt);
  free(iv);
  free(body);
  if (!ok) {
    free(opened);
    return false;
  }
  opened[plainLen] = 0;
  plain = (char *)opened;
  free(opened);
  return true;
}
