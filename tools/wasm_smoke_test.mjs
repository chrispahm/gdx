import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { createGdxReader } from '../src/wasm/demo/gdxReader.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const buildDir = path.resolve(__dirname, '../build-wasm');
const modulePath = path.join(buildDir, 'gdx.js');

async function loadModule() {
  const moduleFactory = (await import(modulePath)).default;
  return moduleFactory({
    locateFile: (p) => path.join(buildDir, p)
  });
}

async function readGdxFile(gdxPath) {
  const buffer = await fs.readFile(gdxPath);
  return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength);
}

async function main() {
  const gdxPath = process.argv[2];
  if (!gdxPath) {
    console.error('Usage: node tools/wasm_smoke_test.mjs <path-to-file.gdx>');
    process.exitCode = 1;
    return;
  }

  console.log('Loading WebAssembly module from %s', buildDir);
  const moduleInstance = await loadModule();
  const reader = createGdxReader(moduleInstance);

  try {
    console.log('Opening GDX file %s', gdxPath);
    const arrayBuffer = await readGdxFile(gdxPath);
    await reader.loadFromArrayBuffer(arrayBuffer);

    const symbols = reader.getSymbols();
    console.log('Symbol count:', symbols.length);

    if (symbols.length > 0) {
      const first = symbols[0];
      console.log('First symbol:', first.name, `(records: ${first.recordCount}, dim: ${first.dimension})`);

      let shown = 0;
      for await (const record of reader.iterateSymbol(first.index)) {
        console.log(`#${shown + 1}`, record.keys, record.values.map(v => Number(v.toFixed(6))));
        if (++shown >= Math.min(5, first.recordCount)) break;
      }
      if (first.recordCount > shown) {
        console.log(`... (${first.recordCount - shown} more records not shown)`);
      }
    }
  } finally {
    reader.dispose();
  }
}

main().catch((err) => {
  console.error('WASM smoke test failed:', err);
  process.exitCode = 1;
});
