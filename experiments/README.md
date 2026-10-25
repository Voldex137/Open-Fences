# experiments/

`legacy-hook/` is a Shell Service Object DLL from an early design in which the fences were drawn from inside the Explorer process. The current application does **not** use it: it is not built by the root `CMakeLists.txt`, and `openfences.exe` never loads it. It is kept only for reference; see `docs/CPP_DESIGN.md` for the original idea.
