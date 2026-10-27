import test from "node:test";
import assert from "node:assert/strict";
import {
  hasDuplicateJsonObjectKeys,
  hasNonFiniteJsonNumber,
  hasUnsafeIntegerJsonNumber,
  exceedsJsonNestingLimit,
  MAX_JSON_NESTING_DEPTH,
  MAX_JSON_TOKEN_COUNT,
  exceedsJsonTokenLimit,
  validateJsonInputSource
} from "./json-input-validation.js";

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

test("detects unsafe integer-form JSON numbers before parsing rounds them", () => {
  assert.equal(hasUnsafeIntegerJsonNumber('{"value":9007199254740992}'), true);
  assert.equal(hasUnsafeIntegerJsonNumber('[-9007199254740992]'), true);
  assert.equal(hasUnsafeIntegerJsonNumber('{"nested":[9007199254740993]}'), true);
});

test("allows safe integer boundaries and non-integer numeric spellings", () => {
  const source = '{"max":9007199254740991,"min":-9007199254740991,"fraction":9007199254740993.0,"exponent":1e20,"text":"9007199254740993","quoted":"\\\"9007199254740993"}';
  assert.equal(hasUnsafeIntegerJsonNumber(source), false);
});

test("ignores unsafe-looking integers within escaped JSON strings", () => {
  const source = JSON.stringify({ note: 'text "9007199254740993" and slash \\', value: 1 });
  assert.equal(hasUnsafeIntegerJsonNumber(source), false);
});

test("rejects JSON nested beyond the configured limit", () => {
  const atLimit = "[".repeat(MAX_JSON_NESTING_DEPTH) + "0" + "]".repeat(MAX_JSON_NESTING_DEPTH);
  const tooDeep = "[".repeat(MAX_JSON_NESTING_DEPTH + 1) + "0" + "]".repeat(MAX_JSON_NESTING_DEPTH + 1);

  assert.equal(exceedsJsonNestingLimit(atLimit), false);
  assert.equal(exceedsJsonNestingLimit(tooDeep), true);
});

test("ignores brackets, braces, and escaped quotes inside JSON strings", () => {
  const source = JSON.stringify({ note: 'text "[" and brace } with slash \\', child: [] });
  assert.equal(exceedsJsonNestingLimit(source), false);
});

test("runs the nesting guard before duplicate-key detection", () => {
  const nestedDuplicate = '{"child":'.repeat(MAX_JSON_NESTING_DEPTH) +
    '{"duplicate":1,"duplicate":2}' +
    "}".repeat(MAX_JSON_NESTING_DEPTH);
  assert.equal(validateJsonInputSource(nestedDuplicate), "nesting-limit");
});

test("handles very long integer tokens without numeric conversion", () => {
  const source = '{"value":' + "9".repeat(100_000) + "}";
  assert.equal(hasUnsafeIntegerJsonNumber(source), true);
});

test("rejects over-budget JSON tokens before duplicate-key scanning", () => {
  const atLimit = "[" + "null,".repeat(MAX_JSON_TOKEN_COUNT - 3) + "null" + "]";
  const overLimitWithDuplicate = '[{"duplicate":1,"duplicate":2},' +
    "null,".repeat(MAX_JSON_TOKEN_COUNT - 8) + "null]";

  assert.equal(exceedsJsonTokenLimit(atLimit), false);
  assert.equal(validateJsonInputSource(overLimitWithDuplicate), "token-limit");
});
