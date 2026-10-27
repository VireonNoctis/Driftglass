/**
 * Decode user-selected JSON bytes as strict UTF-8 without replacing malformed byte sequences.
 * Preserve a leading BOM so JSON.parse rejects it rather than silently normalizing the file.
 * @param {ArrayBuffer | Uint8Array} bytes
 * @returns {string}
 */
export function decodeJsonInputUtf8(bytes) {
  return new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes);
}

/**
 * Detect duplicate object member names before native JSON.parse discards them.
 * Equality is checked after decoding JSON escapes, so "key" and "\u006bey"
 * are treated as the same property name.
 * @param {string} source
 * @returns {boolean}
 */
export function hasDuplicateJsonObjectKeys(source) {
  /** @type {Set<string>[]} */
  const objectKeys = [];

  for (let index = 0; index < source.length; index += 1) {
    const character = source[index];

    if (character === '"') {
      const start = index;
      index += 1;
      while (index < source.length) {
        if (source[index] === "\\") {
          index += 2;
          continue;
        }
        if (source[index] === '"') break;
        index += 1;
      }
      if (index >= source.length) return false;

      const token = source.slice(start, index + 1);
      let next = index + 1;
      while (
        source[next] === " " ||
        source[next] === "\t" ||
        source[next] === "\n" ||
        source[next] === "\r"
      ) {
        next += 1;
      }

      if (source[next] === ":" && objectKeys.length > 0) {
        let key;
        try {
          key = JSON.parse(token);
        } catch {
          // The full parse will return the normal malformed-JSON error.
          return false;
        }
        const keys = objectKeys[objectKeys.length - 1];
        if (keys.has(key)) return true;
        keys.add(key);
      }
      continue;
    }

    if (character === "{") {
      objectKeys.push(new Set());
    } else if (character === "}") {
      objectKeys.pop();
    }
  }

  return false;
}

/**
 * Detect numeric values that JSON.parse converted to Infinity from otherwise
 * valid exponent notation, such as 1e400. Python's strict JSON loader rejects
 * these values instead of silently changing their representation.
 * @param {unknown} value
 * @returns {boolean}
 */
export function hasNonFiniteJsonNumber(value) {
  /** @type {unknown[]} */
  const pending = [value];
  while (pending.length > 0) {
    const current = pending.pop();
    if (typeof current === "number" && !Number.isFinite(current)) return true;
    if (Array.isArray(current)) {
      for (const item of current) pending.push(item);
    } else if (typeof current === "object" && current !== null) {
      for (const item of Object.values(/** @type {Record<string, unknown>} */ (current))) {
        pending.push(item);
      }
    }
  }
  return false;
}

/**
 * Detect integer-form JSON numbers outside JavaScript's exact integer range.
 * JSON.parse rounds these tokens before any schema or rendering code can inspect
 * them, so compare digit length and lexicographic order without building a BigInt.
 * @param {string} source
 * @returns {boolean}
 */
export function hasUnsafeIntegerJsonNumber(source) {
  const maxSafeIntegerDigits = String(Number.MAX_SAFE_INTEGER);

  for (let index = 0; index < source.length;) {
    if (source[index] === '"') {
      index += 1;
      while (index < source.length) {
        if (source[index] === "\\") {
          index += 2;
          continue;
        }
        if (source[index] === '"') {
          index += 1;
          break;
        }
        index += 1;
      }
      continue;
    }

    if (source[index] !== "-" && (source[index] < "0" || source[index] > "9")) {
      index += 1;
      continue;
    }

    const start = index;
    if (source[index] === "-") index += 1;
    if (source[index] < "0" || source[index] > "9") continue;

    if (source[index] === "0") {
      index += 1;
    } else {
      while (source[index] >= "0" && source[index] <= "9") index += 1;
    }

    let integerToken = true;
    if (source[index] === ".") {
      integerToken = false;
      index += 1;
      while (source[index] >= "0" && source[index] <= "9") index += 1;
    }
    if (source[index] === "e" || source[index] === "E") {
      integerToken = false;
      index += 1;
      if (source[index] === "+" || source[index] === "-") index += 1;
      while (source[index] >= "0" && source[index] <= "9") index += 1;
    }

    if (integerToken) {
      const digitStart = source[start] === "-" ? start + 1 : start;
      let significantStart = digitStart;
      while (significantStart < index - 1 && source[significantStart] === "0") {
        significantStart += 1;
      }

      const digitLength = index - significantStart;
      if (digitLength > maxSafeIntegerDigits.length) return true;
      if (digitLength === maxSafeIntegerDigits.length &&
          source.slice(significantStart, index) > maxSafeIntegerDigits) {
        return true;
      }
    }
  }

  return false;
}

/** Upper bound for nesting in user-selected browser imports. */
export const MAX_JSON_NESTING_DEPTH = 256;

/**
 * Reject excessively nested JSON before UI helpers recursively serialize evidence.
 * Braces and brackets inside JSON strings are ignored.
 * @param {string} source
 * @returns {boolean}
 */
export function exceedsJsonNestingLimit(source) {
  let depth = 0;

  for (let index = 0; index < source.length;) {
    if (source[index] === '"') {
      index += 1;
      while (index < source.length) {
        if (source[index] === "\\") {
          index += 2;
          continue;
        }
        if (source[index] === '"') {
          index += 1;
          break;
        }
        index += 1;
      }
      continue;
    }

    if (source[index] === "{" || source[index] === "[") {
      depth += 1;
      if (depth > MAX_JSON_NESTING_DEPTH) return true;
    } else if (source[index] === "}" || source[index] === "]") {
      depth -= 1;
    }
    index += 1;
  }

  return false;
}

/**
 * Upper bound on lexical JSON tokens processed by the browser viewer.
 * Strings/member names and array/object delimiters count toward the budget.
 */
export const MAX_JSON_TOKEN_COUNT = 500_000;

/**
 * Count JSON lexical tokens without parsing or constructing the value tree.
 * Commas, colons, and whitespace are not counted.
 * @param {string} source
 * @returns {boolean}
 */
export function exceedsJsonTokenLimit(source) {
  let tokenCount = 0;

  const countToken = () => {
    tokenCount += 1;
    return tokenCount > MAX_JSON_TOKEN_COUNT;
  };

  for (let index = 0; index < source.length;) {
    const character = source[index];

    if (character === '"') {
      if (countToken()) return true;
      index += 1;
      while (index < source.length) {
        if (source[index] === "\\") {
          index += 2;
          continue;
        }
        if (source[index] === '"') {
          index += 1;
          break;
        }
        index += 1;
      }
      continue;
    }

    if (character === " " || character === "\t" || character === "\n" || character === "\r") {
      index += 1;
      continue;
    }

    if (character === "{" || character === "}" || character === "[" || character === "]") {
      if (countToken()) return true;
      index += 1;
      continue;
    }

    if (character === "-" || (character >= "0" && character <= "9")) {
      if (countToken()) return true;
      if (source[index] === "-") index += 1;
      while (source[index] >= "0" && source[index] <= "9") index += 1;
      if (source[index] === ".") {
        index += 1;
        while (source[index] >= "0" && source[index] <= "9") index += 1;
      }
      if (source[index] === "e" || source[index] === "E") {
        index += 1;
        if (source[index] === "+" || source[index] === "-") index += 1;
        while (source[index] >= "0" && source[index] <= "9") index += 1;
      }
      continue;
    }

    if (character === "t") {
      if (countToken()) return true;
      index += 4;
      continue;
    }
    if (character === "f") {
      if (countToken()) return true;
      index += 5;
      continue;
    }
    if (character === "n") {
      if (countToken()) return true;
      index += 4;
      continue;
    }

    // Native JSON.parse will report malformed tokens after the bounded scan.
    index += 1;
  }

  return false;
}

/**
 * Run pre-parse checks in resource-safe order. Limits must run before duplicate-key
 * detection, which allocates a key set for each open object.
 * @param {string} source
 * @returns {"nesting-limit" | "token-limit" | "duplicate-keys" | "unsafe-integer" | null}
 */
export function validateJsonInputSource(source) {
  if (exceedsJsonNestingLimit(source)) return "nesting-limit";
  if (exceedsJsonTokenLimit(source)) return "token-limit";
  if (hasDuplicateJsonObjectKeys(source)) return "duplicate-keys";
  if (hasUnsafeIntegerJsonNumber(source)) return "unsafe-integer";
  return null;
}
