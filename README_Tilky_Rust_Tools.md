# Tilky Rust Tools Workspace

Extract this zip into the root of your Wolfy Engine / Tilky Engine project.

It contains:

- `Cargo.toml` at the project root
- `Tools/TilkyLocalise`
- `Tools/TilkyExporter`

Build from the project root:

```powershell
cargo build --release -p tilky_localise
cargo build --release -p tilky_exporter
```

Outputs:

```text
target/release/tilky_localise.exe
target/release/tilky_exporter.exe
```

Example exporter usage:

```powershell
target\release\tilky_exporter.exe `
  --project "C:\Users\berke\Documents\Tilky Engine\Projects\sphere\project.tilky" `
  --player-exe "C:\TilkyBuilds\release-msys2\Tilky_Player.exe" `
  --output "C:\Users\berke\Desktop\Exports\Sphere" `
  --game-name "Sphere"
```

Optional DLL copying:

```powershell
target\release\tilky_exporter.exe `
  --project "..." `
  --player-exe "..." `
  --output "..." `
  --game-name "Sphere" `
  --dll "C:\msys64\mingw64\bin\libstdc++-6.dll" `
  --dll "C:\msys64\mingw64\bin\libgcc_s_seh-1.dll" `
  --dll "C:\msys64\mingw64\bin\libwinpthread-1.dll"
```
