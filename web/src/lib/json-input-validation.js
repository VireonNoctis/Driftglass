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
 * them, so compare the original decimal token using BigInt instead.
 * @param {string} source
 * @returns {boolean}
 */
export function hasUnsafeIntegerJsonNumber(source) {
  const maxSafeInteger = BigInt(Number.MAX_SAFE_INTEGER);

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
      const token = source.slice(start, index);
      try {
        const value = BigInt(token);
        if (value > maxSafeInteger || value < -maxSafeInteger) return true;
      } catch {
        // Malformed numeric tokens remain the responsibility of JSON.parse.
      }
    }
  }

  return false;
}
