import test from "node:test";
import assert from "node:assert/strict";
import { hasDuplicateJsonObjectKeys, hasNonFiniteJsonNumber } from "./json-input-validation.js";

test("detects duplicate object keys", () => {
  assert.equal(hasDuplicateJsonObjectKeys('{"a":1,"a":2}'), true);
});

test("detects duplicate keys written with different JSON escapes", () => {
  assert.equal(hasDuplicateJsonObjectKeys('{"a":1,"\\u0061":2}'), true);
});

test("allows the same key name in separate nested objects", () => {
  assert.equal(hasDuplicateJsonObjectKeys('{"first":{"id":1},"second":{"id":2}}'), false);
});

test("allows repeated keys in distinct objects in an array", () => {
  assert.equal(hasDuplicateJsonObjectKeys('[{"id":1},{"id":2}]'), false);
});

test("ignores braces and escaped quotes inside string values", () => {
  const source = JSON.stringify({ note: 'quoted " and \\', value: "{}" });
  assert.equal(hasDuplicateJsonObjectKeys(source), false);
});

test("detects overflowed JSON exponent values after native parsing", () => {
  const parsed = JSON.parse('{"nested":[{"value":1e400}]}');
  assert.equal(hasNonFiniteJsonNumber(parsed), true);
});

test("allows finite JSON numbers and strings that look like exponents", () => {
  const parsed = JSON.parse('{"small":-1.25,"large":1e308,"text":"1e400","empty":null}');
  assert.equal(hasNonFiniteJsonNumber(parsed), false);
});
