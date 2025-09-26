const RECORD_VIEW_SIZE = 16; // four 32-bit integers
const SYMBOL_INFO_SIZE = 64 + 256 + 16; // char[64], char[256], four 32-bit ints
const UEL_BUFFER_SIZE = 256;
const VALUE_COUNT = 5; // level, marginal, lower, upper, scale

export function createGdxReader(Module) {
  const module = Module;

  const init = module.cwrap('gdx_wasm_init', 'number', []);
  const shutdown = module.cwrap('gdx_wasm_shutdown', null, []);
  const openBuffer = module.cwrap('gdx_wasm_open_buffer', 'number', ['number', 'number']);
  const symbolCountFn = module.cwrap('gdx_wasm_symbol_count', 'number', []);
  const symbolInfoFn = module.cwrap('gdx_wasm_symbol_info', 'number', ['number', 'number']);
  const startSymbolFn = module.cwrap('gdx_wasm_start_symbol', 'number', ['number', 'number']);
  const nextRecordFn = module.cwrap('gdx_wasm_next_record', 'number', ['number', 'number']);
  const cursorCloseFn = module.cwrap('gdx_wasm_cursor_close', null, ['number']);
  const getUelFn = module.cwrap('gdx_wasm_get_uel', 'number', ['number', 'number', 'number']);
  const lastErrorFn = module.cwrap('gdx_wasm_last_error', 'number', []);

  function readLastError() {
    const ptr = lastErrorFn();
    return ptr ? module.UTF8ToString(ptr) : '';
  }

  function makeErrorFromLast(message) {
    const error = readLastError();
    return new Error(error ? `${message}: ${error}` : message);
  }

  const api = {
    async loadFromArrayBuffer(buffer) {
      if (!(buffer instanceof ArrayBuffer)) {
        throw new TypeError('Expected an ArrayBuffer');
      }
      const byteLength = buffer.byteLength;
      if (!byteLength) {
        throw new Error('ArrayBuffer is empty');
      }
      const ptr = module._malloc(byteLength);
      try {
        module.HEAPU8.set(new Uint8Array(buffer), ptr);
        const rc = openBuffer(ptr, byteLength);
        if (rc !== 0) throw makeErrorFromLast('Failed to open GDX buffer');
      } finally {
        module._free(ptr);
      }
    },

    async loadFromFile(file) {
      if (typeof File === 'undefined' || !(file instanceof File)) {
        throw new TypeError('Expected a File object');
      }
      const buffer = await file.arrayBuffer();
      return this.loadFromArrayBuffer(buffer);
    },

    getSymbolCount() {
      const count = symbolCountFn();
      if (count < 0) throw makeErrorFromLast('Failed to obtain symbol count');
      return count;
    },

    getSymbolInfo(index) {
      const ptr = module._malloc(SYMBOL_INFO_SIZE);
      try {
        const ok = symbolInfoFn(index, ptr);
        if (!ok) throw makeErrorFromLast('Failed to read symbol info');
        const name = module.UTF8ToString(ptr);
        const explanation = module.UTF8ToString(ptr + 64);
        const base = (ptr + 64 + 256) >> 2;
        const ints = module.HEAP32.subarray(base, base + 4);
        return {
          index,
          name,
          explanation,
          type: ints[0],
          dimension: ints[1],
          recordCount: ints[2],
          userInfo: ints[3]
        };
      } finally {
        module._free(ptr);
      }
    },

    getSymbols() {
      const count = this.getSymbolCount();
      const symbols = [];
      for (let i = 0; i < count; i++) symbols.push(this.getSymbolInfo(i));
      return symbols;
    },

    getUel(index) {
      const ptr = module._malloc(UEL_BUFFER_SIZE);
      try {
        const ok = getUelFn(index, ptr, UEL_BUFFER_SIZE);
        if (!ok) throw makeErrorFromLast('Failed to retrieve UEL');
        return module.UTF8ToString(ptr);
      } finally {
        module._free(ptr);
      }
    },

    async *iterateSymbol(index) {
      const handle = startSymbolFn(index, 0);
      if (handle < 0) throw makeErrorFromLast('Unable to start symbol streaming');
      const ptr = module._malloc(RECORD_VIEW_SIZE);
      try {
        while (true) {
          const result = nextRecordFn(handle, ptr);
          if (result === 0) break;
          if (result < 0) throw makeErrorFromLast('Failed to read record');

          const viewBase = ptr >> 2;
          const keyOffset = module.HEAP32[viewBase];
          const valueOffset = module.HEAP32[viewBase + 1];
          const dimension = module.HEAP32[viewBase + 2];
          const dimChanged = module.HEAP32[viewBase + 3];

          const keyStart = keyOffset >> 2;
          const keys = Array.from(module.HEAP32.subarray(keyStart, keyStart + dimension));

          const valueStart = valueOffset >> 3;
          const values = Array.from(module.HEAPF64.subarray(valueStart, valueStart + VALUE_COUNT));

          yield { keys, values, dimChanged };
        }
      } finally {
        module._free(ptr);
        cursorCloseFn(handle);
      }
    },

    lastError() {
      return readLastError();
    },

    dispose() {
      shutdown();
    }
  };

  const initResult = init();
  if (initResult !== 0) throw makeErrorFromLast('Failed to initialize GDX WASM module');

  return api;
}
