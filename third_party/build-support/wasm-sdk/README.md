# Explicit browser SDK preparation

This is a separate `wasm32-emscripten` SDK family. It does not reuse a native
FLTK/Rev dependency prefix or the Linux source SDK's target libraries. The
initial compiler-host recipe supports Linux x86_64; the resulting browser assets
are architecture independent. Other compiler hosts need their own pinned input
recipe and qualification before being advertised.

`manifest.json` pins official emsdk 6.0.10 sources, the official compiler build
`666337b525e673e769121856d175f6f52b8ead64`, its Node runtime, and OpenSSL 3.5.7
sources by SHA-256. These inputs were obtained from the URLs in that manifest on
2026-09-28. The Emscripten compiler archive is an upstream binary toolchain, not
a claim that LLVM was rebuilt locally. OpenSSL is cross-compiled locally with
socket support, dynamic engines/modules, threads and assembly disabled. Browser
entropy uses a pinned OpenSSL dependency patch that calls Emscripten's
`getentropy` in chunks of at most 256 bytes. Emscripten routes each request to
the browser's `crypto.getRandomValues`, including later OpenSSL reseeds. Missing
or denied secure entropy fails closed. No application or UI feature supplies
seeds. The application continues
to compile its pinned vendored XZ sources.

Preparation requires host Python 3.12+, Perl, Make, Patch, and a Linux x86_64 host able
to run the pinned upstream compiler and Node. Application builds also require
CMake 3.21+ and Ninja or Make. This recipe does not install system packages or
modify the user's shell setup. The reusable archive contains this Linux host
toolchain and its pinned inputs; it does not claim compatibility with other
compiler hosts or a different host ABI.

```sh
# The only operation permitted to download inputs is explicit fetch --download.
python3 tools/build-wasm-sdk.py fetch --download --sources /path/to/wasm-inputs
# Reuse the exact inputs offline; this refuses an existing SDK destination.
python3 tools/build-wasm-sdk.py prepare --sources /path/to/wasm-inputs \
  --destination /path/to/wasm-sdk --jobs 4
./build.sh --wasm-sdk /path/to/wasm-sdk
./build.sh test web --wasm-sdk /path/to/wasm-sdk
./build.sh package --wasm-sdk /path/to/wasm-sdk
```

The prepared SDK retains all input archives, recipe metadata and OpenSSL
licensing, plus the exact entropy patch and qualification probe sources with
their hashes. Preparation checks the pinned source before and after patching,
then runs the compiled OpenSSL `RAND_bytes`, `RAND_priv_bytes` and repeated
prediction-resistant reseeds in a browser-like VM backed by WebCrypto. The probe
counts fresh entropy requests and checks initial denial, denial after successful
generation, and missing WebCrypto. It opens no browser, listener or network
connection. Only successful preparation records the required entropy capability;
the toolchain rejects earlier SDKs whose Unix entropy discovery did not work in
WebAssembly. Prepare a new destination to replace those SDKs.
Compiler cache variants are populated during explicit preparation;
`build.sh` then sets `EM_FROZEN_CACHE=1`. A missing variant fails instead of
silently preparing a dependency during an application build. Keep prepared SDKs
at their installed location; install a verified archive at a new location when
needed. Use a new preparation for a changed recipe. Keep
build directories separate from native builds and from other SDK identities.
The wrapper rejects competing compiler/search-path environment overrides and
the toolchain checks target paths and the OpenSSL archive checksum.

The Wasm product uses a single Worker with cooperative C++ execution and
Asyncify. It does not require pthreads, SharedArrayBuffer or cross-origin
isolation headers. `tools/package-wasm.py` creates `datapump-wasm.html` with
preloaded code, Wasm and audio-worklet bytes, plus a hash inventory and explicit
runtime import list. It rejects socket/thread imports and shared Wasm memory;
this static check complements the browser's restrictive CSP and runtime checks.
The generated page needs a secure browser context for microphone access and
normal browser user consent. It makes no server-side file-access claim.

No base or release asset is published by these commands. Distributing a new SDK
or application requires the repository's normal source, checksum, browser and
release qualification procedures. An existing durable base recipe must never
be silently replaced with these local preparation outputs.

## Immutable base archives and relocation

Ordinary CI and release consumers fetch the exact recipe from `base`; missing
recipes fail with an explicit base-maintenance instruction. They never download
compiler inputs or build dependencies implicitly:

```sh
python3 tools/wasm-sdk-release.py fetch --repo OWNER/REPO \
  --directory /path/to/wasm-archives --binary-only --require
python3 tools/wasm-sdk-release.py install --directory /path/to/wasm-archives \
  --destination /path/to/new/wasm-sdk
```

Only explicit base maintenance prepares a new SDK, using `fetch --download` and
`prepare` above, then packages and publishes it:

```sh
python3 tools/wasm-sdk-release.py archive --sdk /path/to/wasm-sdk \
  --directory /path/to/new/wasm-archives
python3 tools/wasm-sdk-release.py verify --directory /path/to/new/wasm-archives
python3 tools/wasm-sdk-release.py publish --repo OWNER/REPO \
  --directory /path/to/new/wasm-archives --source-sha FULL_GIT_COMMIT
```

`id` prints the recipe identity: the first 20 hex digits of SHA-256 over the
builder and sorted complete recipe directory, including relative paths. The
prepared manifest records `recipe_id`; packaging rejects old or changed recipes.
The three immutable assets are `datapump-wasm-sdk-ID-linux-x86_64.tar.gz`,
`datapump-wasm-sdk-sources-ID.tar.gz`, and `wasm-sdk-ID-SHA256SUMS.txt` (downloaded
locally as `SHA256SUMS`). Both archives preserve the exact recipe and pinned
inputs; the binary also preserves target libraries, frozen cache and notices.
Existing base assets are compared and reused only byte-for-byte; partial recipes
need deliberate repair, and uploads never overwrite assets.

Installation verifies archive checksums, recipe bytes, source checksums and
preparation provenance before extraction or tool execution. Archives preserve
internal file symlinks and reject escaping/cyclic links, writes through linked
directories, duplicate paths and special files. Emscripten configuration derives its paths from the installed root;
OpenSSL pkg-config paths are relative. The installer records the new root and
runs the compiler plus the compiled WebCrypto entropy qualification with the
cache frozen. A failed probe removes only the newly created installation. No
host-tool ABI portability beyond the pinned Linux x86_64 recipe is implied.
