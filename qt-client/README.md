Qt client (minimal)

Requirements:
- Qt5 development (widgets + network)
- CMake

Build:

```bash
mkdir -p qt-client/build
cd qt-client/build
cmake ..
make -j2
```

Run:

```bash
./messenger_qt
```

The client is a minimal proof-of-concept: it sends the fixed username `qtuser` and sends raw binary `Message` structs to the existing C++ server. This is a demo; for production you'd want to use a safe serialization format and proper auth.
