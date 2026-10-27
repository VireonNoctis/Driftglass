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
