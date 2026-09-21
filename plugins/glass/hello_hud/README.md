# hello_hud

MemoMind glasses plugin (`com.memomind.hello-hud`).

```sh
./mm build hello_hud      # -> build/glass/hello_hud/hello_hud.gmp
```

The `.gmp` and its two `.review.*` sidecars belong together; Desktop Studio
packages all three.

Budgets to keep an eye on in the build output: 500 KiB Flash, < 100 KiB static
RAM, 1024 B per function frame. See `docs/glasses-platform.md`.
