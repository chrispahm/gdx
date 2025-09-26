import createGdxModule from './gdx.js';
import { createGdxReader } from './gdxReader.js';

const statusEl = document.querySelector('[data-status]');
const symbolListEl = document.querySelector('[data-symbols]');
const recordsBodyEl = document.querySelector('[data-records]');
const summaryEl = document.querySelector('[data-summary]');
const previewLimitEl = document.querySelector('[data-preview-limit]');
const fileInputEl = document.querySelector('[data-file-input]');
const errorEl = document.querySelector('[data-error]');

const PREVIEW_LIMIT_DEFAULT = 50;

let moduleInstance;
let reader;
let currentSymbols = [];
let previewLimit = PREVIEW_LIMIT_DEFAULT;

function setStatus(message) {
  statusEl.textContent = message;
}

function showError(message) {
  errorEl.textContent = message;
  errorEl.hidden = !message;
}

function clearRecords() {
  recordsBodyEl.innerHTML = '';
  summaryEl.textContent = '';
}

function renderSymbols(symbols) {
  symbolListEl.innerHTML = '';
  if (!symbols.length) {
    const empty = document.createElement('p');
    empty.textContent = 'No symbols found in file.';
    symbolListEl.appendChild(empty);
    return;
  }

  const list = document.createElement('ul');
  list.classList.add('symbol-list');
  symbols.forEach(symbol => {
    const li = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = `${symbol.name} (dim ${symbol.dimension}, ${symbol.recordCount} recs)`;
    button.addEventListener('click', () => displaySymbol(symbol));
    li.appendChild(button);
    list.appendChild(li);
  });
  symbolListEl.appendChild(list);
}

async function displaySymbol(symbol) {
  clearRecords();
  setStatus(`Streaming records for ${symbol.name} ...`);
  let count = 0;
  try {
    for await (const record of reader.iterateSymbol(symbol.index)) {
      appendRecordRow(symbol, record, ++count);
      if (count >= previewLimit) {
        summaryEl.textContent = `Previewing first ${previewLimit} records (total records: ${symbol.recordCount}).`;
        break;
      }
    }
    if (count === 0) {
      summaryEl.textContent = 'This symbol has no records.';
    } else if (count < symbol.recordCount) {
      summaryEl.textContent = `Previewed ${count} of ${symbol.recordCount} records.`;
    } else {
      summaryEl.textContent = `Displayed all ${count} records.`;
    }
  } catch (err) {
    showError(err.message || String(err));
  } finally {
    setStatus('Ready');
  }
}

function appendRecordRow(symbol, record, index) {
  const row = document.createElement('tr');

  const indexCell = document.createElement('td');
  indexCell.textContent = index.toString();
  row.appendChild(indexCell);

  const keysCell = document.createElement('td');
  keysCell.textContent = record.keys.join(', ');
  row.appendChild(keysCell);

  const valuesCell = document.createElement('td');
  valuesCell.textContent = record.values.map((v, idx) => `${idx}:${formatNumber(v)}`).join('  ');
  row.appendChild(valuesCell);

  recordsBodyEl.appendChild(row);
}

function formatNumber(value) {
  if (!Number.isFinite(value)) return String(value);
  if (Math.abs(value) >= 1 || value === 0) return value.toFixed(6).replace(/\.0+$/, '');
  return value.toExponential(6);
}

async function handleFileChange(event) {
  const [file] = event.target.files || [];
  if (!file) return;

  setStatus('Loading GDX file...');
  showError('');
  clearRecords();

  try {
    await reader.loadFromFile(file);
    currentSymbols = reader.getSymbols();
    renderSymbols(currentSymbols);
    setStatus(`Loaded ${currentSymbols.length} symbols.`);
  } catch (err) {
    showError(err.message || String(err));
    setStatus('Error');
  }
}

function handlePreviewLimitChange(event) {
  const value = Number.parseInt(event.target.value, 10);
  if (Number.isFinite(value) && value > 0) {
    previewLimit = value;
  } else {
    previewLimit = PREVIEW_LIMIT_DEFAULT;
  }
}

async function bootstrap() {
  try {
    setStatus('Initializing WebAssembly module...');
    moduleInstance = await createGdxModule({
      locateFile: (path) => path
    });
    reader = createGdxReader(moduleInstance);
    setStatus('Ready');
  } catch (err) {
    showError(`Failed to initialize module: ${err.message || err}`);
    setStatus('Initialization failed');
    throw err;
  }

  fileInputEl.addEventListener('change', handleFileChange);
  previewLimitEl.value = PREVIEW_LIMIT_DEFAULT;
  previewLimitEl.addEventListener('change', handlePreviewLimitChange);
}

window.addEventListener('unload', () => {
  if (reader) reader.dispose();
});

bootstrap().catch((err) => {
  console.error(err);
});
