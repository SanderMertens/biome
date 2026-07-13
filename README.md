# Biome

A tiny RTS about terraforming a barren planet, built with
[flecs](https://github.com/SanderMertens/flecs) and the
[flecs engine](../flecs-engine).

<img width="1301" height="813" alt="Screenshot 2026-07-07 at 8 12 45 AM" src="https://github.com/user-attachments/assets/4dc2d81d-40dd-4e74-aeab-af7546b5a012" />

# Run
Install [bake3](https://github.com/SanderMertens/bake3) by cloning the repository, and running this script in the bake3 repository root:

```bash
./setup.sh
```

Then run the game with the following command:

```bash
bake3 run biome --cfg release --local-env
```

When developing, run in debug mode:

```bash
bake3 run biome --local-env
```

To debug memory issues, run in sanitized mode:

```bash
bake3 run biome --local-env
```
