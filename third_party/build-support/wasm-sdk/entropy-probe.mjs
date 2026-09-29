// Run the actual compiled SDK probe in browser-like VM realms, without a
// browser, listener, filesystem entropy device, network or Node RNG imports.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {performance} from 'node:perf_hooks';

const [factoryPath, wasmPath] = process.argv.slice(2);
assert(factoryPath && wasmPath, 'usage: node entropy-probe.mjs factory.js module.wasm');
const factory = fs.readFileSync(factoryPath, 'utf8');
const bytes = new Uint8Array(fs.readFileSync(wasmPath));
const compiled = new WebAssembly.Module(bytes);
const deny = () => { throw new Error('SDK entropy probe forbids network/asset loading'); };

async function create(mode) {
    const state = {calls: 0, sizes: [], denied: mode === 'denied'};
    const sandbox = {console, WebAssembly, ArrayBuffer, SharedArrayBuffer: undefined,
        Uint8Array, Int8Array, Uint16Array, Int16Array, Uint32Array, Int32Array,
        Float32Array, Float64Array, BigInt64Array, BigUint64Array, TextEncoder, TextDecoder,
        performance, setTimeout, clearTimeout, WorkerGlobalScope: function () {},
        location: {href: 'blob:preloaded-sdk-entropy-probe'}, fetch: deny,
        XMLHttpRequest: deny, WebSocket: deny, WebTransport: deny, EventSource: deny,
        RTCPeerConnection: deny, importScripts: deny};
    if (mode !== 'missing') sandbox.crypto = {getRandomValues(view) {
        ++state.calls; state.sizes.push(view.length);
        if (state.denied) throw new Error('WebCrypto entropy denied by test');
        return webcrypto.getRandomValues(view);
    }};
    sandbox.self = sandbox;
    vm.createContext(sandbox);
    vm.runInContext('Math.random = () => { throw new Error("Weak entropy forbidden"); };', sandbox);
    vm.runInContext(factory, sandbox, {filename: factoryPath});
    assert.equal(typeof sandbox.DatapumpEntropyProbe, 'function');
    const module = await sandbox.DatapumpEntropyProbe({noInitialRun: true, wasmBinary: bytes,
        instantiateWasm(imports, receive) {
            const instance = new WebAssembly.Instance(compiled, imports);
            receive(instance, compiled); return instance.exports;
        }, locateFile: deny, print: () => {}, printErr: () => {}});
    return {state, invoke: reseed => module._datapump_entropy_probe(reseed)};
}

const good = await create('available');
assert.equal(good.invoke(0), 1, 'RAND_bytes/RAND_priv_bytes failed');
assert(good.state.calls > 0, 'OpenSSL never requested browser entropy');
for (let i = 0; i < 3; ++i) {
    const before = good.state.calls;
    assert.equal(good.invoke(1), 1, 'prediction-resistant reseeding failed');
    assert(good.state.calls > before, 'reseed reused cached entropy');
}
assert(good.state.sizes.every(size => size > 0 && size <= 256), 'getentropy chunk limit violated');
const successfulCalls = good.state.calls;
function failsClosed(probe, reseed) {
    let result;
    try { result = probe.invoke(reseed); } catch { return; }
    assert.notEqual(result, 1, 'OpenSSL succeeded without required secure entropy');
}
good.state.denied = true;
failsClosed(good, 1);
assert(good.state.calls > successfulCalls, 'forced reseed did not request newly denied entropy');
const denied = await create('denied');
failsClosed(denied, 0);
assert(denied.state.calls > 0, 'denied WebCrypto source was not reached');
failsClosed(await create('missing'), 0);
console.log(JSON.stringify({result: 'passed', capability: 'emscripten-getentropy-webcrypto-v1',
    successfulWebCryptoCalls: successfulCalls, forcedReseeds: 3,
    deniedInitialEntropy: 'failed-closed', deniedReseed: 'failed-closed',
    missingWebCrypto: 'failed-closed'}));
