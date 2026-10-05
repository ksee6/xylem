/**
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Xylem DB · TypeScript / WASM smoke test                                │
 * │  Run with: bun tests/test_dx.ts                                         │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * The database runs entirely in-memory (no file I/O).
 * BlockDevice automatically falls back to an internal Map when no
 * onDeviceRead/Write callbacks are provided – so setup is zero-config.
 */

import { readFileSync } from "fs";
import { resolve } from "path";
import {
  init,
  XylemEngine,
} from "./wasm/xylem_wasm.ts";

// ─── WASM loader shim ────────────────────────────────────────────────────────
// Bun doesn't expose a file:// fetch handler that WebAssembly.instantiate
// accepts.  We patch globalThis.fetch so that init() can load the .wasm file.

const __dir = new URL(".", import.meta.url).pathname;

(globalThis as any).fetch = async (url: string) => {
  const path = url.replace(/^file:\/\//, "");
  const buf = readFileSync(path);
  const ab = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
  return { arrayBuffer: async () => ab, ok: true, status: 200 };
};

// ─── Tiny test harness ───────────────────────────────────────────────────────

let passed = 0;
let failed = 0;
const failures: string[] = [];

function assert(condition: boolean, label: string) {
  if (condition) {
    console.log(`  \x1b[32m✓\x1b[0m ${label}`);
    passed++;
  } else {
    console.error(`  \x1b[31m✗\x1b[0m ${label}`);
    failures.push(label);
    failed++;
  }
}

function section(name: string) {
  console.log(`\n\x1b[1;34m▸ ${name}\x1b[0m`);
}

// ─── Helper: read blob text for a given virtual path ─────────────────────────

function getFile(db: XylemEngine, path: string): string | null {
  const res = db.cat(path, 0, 0);
  if (res.code !== 0) return null;
  const jsonStr = res.getRowsJson();
  const rows = JSON.parse(jsonStr);
  if (rows && rows.length > 0) {
    return rows[0].content;
  }
  return null;
}

// ─── Main ────────────────────────────────────────────────────────────────────

const wasmPath = resolve(__dir, "wasm/xylem_wasm.wasm");

console.log("\x1b[1m");
console.log("  ╔═══════════════════════════════════════╗");
console.log("  ║    Xylem DB · WASM DX Test  ✦         ║");
console.log("  ╚═══════════════════════════════════════╝");
console.log("\x1b[0m");

await init("file://" + wasmPath);
console.log("  WASM module loaded →", wasmPath);

// ─── 1. Engine setup ─────────────────────────────────────────────────────────
section("1 · Engine setup  (in-memory, zero-config)");

const db = new XylemEngine();

// 10 MB virtual device, 4 KB blocks – sensible small-device config.
// No onDeviceRead/Write callbacks → BlockDevice uses its internal Map store.
// Note: deviceSize and maxCache are u64 in C++ → must pass BigInt to WASM.
(db.config as any).deviceSize = BigInt(10 * 1024 * 1024);  // 10 MB
db.config.blockSize  = 4096;                               // 4 KB blocks (u32)
db.maxCache = 5 * 1024 * 1024;  // 5 MB cache (usz = u32 in 32-bit WASM)

assert(!db.isMounted(), "not mounted before format/mount");

const formatted = db.format();
assert(formatted === true, "format() succeeds");

const mounted = db.mount();
assert(mounted === true, "mount() succeeds");
assert(db.isMounted(), "engine is mounted");

// ─── 2. File-like API ────────────────────────────────────────────────────────
section("2 · File-like API: tee + cat");

const helloResult = db.tee("/hello.txt", "Hello, Xylem!", 0, 0);
assert(helloResult.code === 0, "tee('/hello.txt') → code 0");

const helloText = getFile(db, "/hello.txt");
assert(helloText !== null,          "cat('/hello.txt') – file exists");
assert(helloText === "Hello, Xylem!", `content matches: "${helloText}"`);

const jsonContent = JSON.stringify({ id: 1, name: "Alice", score: 42 });
db.tee("/users/alice.json", jsonContent, 0, 0);
const jsonBack = getFile(db, "/users/alice.json");
assert(jsonBack === jsonContent, "JSON round-trip matches");

db.tee("/users/bob.json",
  JSON.stringify({ id: 2, name: "Bob", score: 99 }), 0, 0);

// ─── 3. Directory listing ─────────────────────────────────────────────────────
section("3 · Directory listing: ls");

const lsRoot  = db.ls("/");
assert(lsRoot.code >= 0, "ls('/') → success");

const lsUsers = db.ls("/users/");
assert(lsUsers.code >= 0, "ls('/users/') → success");

// ─── 4. Removal ──────────────────────────────────────────────────────────────
section("4 · File removal: rm");

const rmResult = db.rm("/hello.txt");
assert(rmResult === true, "rm('/hello.txt') → true");

const goneRef = db.getBlobRef("/hello.txt");
assert(goneRef === 0, "getBlobRef after rm → 0 (gone)");

assert(getFile(db, "/users/alice.json") !== null,
  "alice.json still intact after removing hello.txt");

// ─── 5. Structured write ─────────────────────────────────────────────────────
section("5 · Structured DB: write");

// Xylem can store structured rows (key/value columns).
// write(columns, wheres, txId, encryptionKey)
// The generated binding accepts plain JS arrays of objects for Array_Clause / Array_Clauses
// and auto-converts them via the built-in fromJS coercions.

const w1 = db.write(
  [{ col: "id",   op: "", val: "u:1" },
   { col: "name", op: "", val: "Alice" },
   { col: "role", op: "", val: "admin" }],
  [], 0, "",
);
assert(w1 === 0, "write row { id:'u:1', name:'Alice', role:'admin' } → 0");

const w2 = db.write(
  [{ col: "id",   op: "", val: "u:2" },
   { col: "name", op: "", val: "Bob" },
   { col: "role", op: "", val: "viewer" }],
  [], 0, "",
);
assert(w2 === 0, "write row { id:'u:2', name:'Bob', role:'viewer' } → 0");

// ─── 6. SQL-style query parser ────────────────────────────────────────────────
section("6 · Query parser");

const qAll = db.query("READ*", []);
assert(qAll.code >= 0, "READ* → success");

// Parameterised query – sanitized args replace '?' placeholders
const qUser = db.query("READ* WHERE id = ?", ["u:1"]);
assert(qUser.code >= 0, "READ* WHERE id = ? → success");

// ─── 7. Unique ID generation ──────────────────────────────────────────────────
section("7 · ID generation");

const id1 = db.generateId("id");
const id2 = db.generateId("id");
assert(id1.length > 0,  `generateId gives non-empty: "${id1}"`);
assert(id1 !== id2,     "consecutive generateId calls produce unique IDs");

// ─── 8. Copy + move ───────────────────────────────────────────────────────────
section("8 · cp + mv");

const cpResult = db.cp("/users/alice.json", "/users/alice.bak.json");
assert(cpResult.code === 0, "cp alice.json → alice.bak.json → code 0");

const aliceBak = getFile(db, "/users/alice.bak.json");
assert(aliceBak === jsonContent, "copied file content matches original");

const mvResult = db.mv("/users/bob.json", "/archive/bob.json");
assert(mvResult.code === 0, "mv /users/bob.json → /archive/bob.json → code 0");

assert(db.getBlobRef("/users/bob.json") === 0,
  "/users/bob.json gone after mv");
assert(getFile(db, "/archive/bob.json") !== null,
  "/archive/bob.json exists after mv");

// ─── 9. Flush ────────────────────────────────────────────────────────────────
section("9 · Flush");

db.flush();
assert(db.isMounted(), "engine still mounted after flush()");

// ─── Summary ──────────────────────────────────────────────────────────────────

console.log("\n\x1b[1m━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\x1b[0m");
if (failed === 0) {
  console.log(`\x1b[1;32m  ✦  All ${passed} assertions passed.\x1b[0m\n`);
} else {
  console.log(`  \x1b[1;32m✓ ${passed} passed\x1b[0m  \x1b[1;31m✗ ${failed} failed\x1b[0m`);
  console.log("\n  Failed:");
  failures.forEach((f) => console.log(`    • ${f}`));
  process.exit(1);
}
