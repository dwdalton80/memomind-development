# __NAME__

MemoMind glasses plugin (`__ID__`).

```sh
./mm build __NAME__      # -> build/glass/__NAME__/__NAME__.gmp
```

The `.gmp` and its two `.review.*` sidecars belong together; Desktop Studio
packages all three.

Budgets to keep an eye on in the build output: 500 KiB Flash, < 100 KiB static
RAM, 1024 B per function frame. See `docs/glasses-platform.md`.
