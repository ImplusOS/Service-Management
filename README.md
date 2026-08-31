# ImplusOS Service-Management

The client-side machinery for ImplusOS's hot-loadable userland services:
`service_client.c/h` (`service_load`/`service_unload`), the unified
syscall wrapper (`Syscalls.c/h`), and `services.list`, the manifest of
services loaded at init (see
[Userland-Common](https://github.com/ImplusOS/Userland-Common)'s
`Userland.c`).

This repository is a component of **[ImplusOS](https://github.com/ImplusOS)**,
a hobby operating system with a monolithic kernel, loadable driver modules,
a minimal freestanding C library, and a small graphical userland. It is not
meant to be built in isolation -- it is consumed as a checkout alongside
ImplusOS's other component repositories (see `Docs` for the full
architecture and `ImplusOS/Makefile` for how the pieces are wired together).

## Layout

```
Service-Management/
├── Source/    All source for this component, structure preserved from ImplusOS
└── README.md  This file
```

## Build

No standalone Makefile: consumed as source by
[Userland-Common](https://github.com/ImplusOS/Userland-Common)'s
`AppCommon.mk` (as part of `COMMON_OBJS`) and by each service's own
Makefile.

## License

MIT, matching the parent [ImplusOS](https://github.com/ImplusOS/ImplusOS) project.
