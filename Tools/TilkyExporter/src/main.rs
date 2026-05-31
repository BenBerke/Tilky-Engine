/* This file is responsible for copying the engine & project folders into the desired location passed
via the command line argument.

Arguments:
1. Path to the current project's project.tilky file.
2. Path to the desired export folder.
3. Path to Standalone.exe.
 */

use std::{env, fs};
use std::ascii::AsciiExt;
use anyhow::{anyhow, Context, Error, Result};
use std::path::{Path, PathBuf};

fn copy_dir_recursive(from: &Path, to: &Path) -> Result<()> {
       if !from.exists() { return Err(anyhow!("Source file does not exists {}", from.display())); }
       if !from.is_dir() {return Err(anyhow!("Source file is not a directory {}", from.display())); }

       fs::create_dir_all(to).with_context(|| format!("Failed to create destination folder: {}", to.display()))?;

       for entry in fs::read_dir(from).with_context(|| format!("Failed to read folder {}", from.display()))?{
              let entry = entry?;
              let source_path = entry.path();
              let dest_path = to.join(entry.file_name());

              if source_path.is_dir(){
                     copy_dir_recursive(&source_path, &dest_path)?;
              }
              else {
                     copy_file(&source_path, &dest_path)?;
              }
       }

       Ok(())
}

fn copy_file(from: &Path, to: &Path) -> Result<()> {
       if !from.exists() { return Err(anyhow!("Source file does not exist: {}", from.display())); }
       if !from.is_file() { return Err(anyhow!("Source is not a file: {}", from.display())); }

       if let Some(parent) = to.parent() {
              fs::create_dir_all(parent).with_context(|| format!("Failed to create folder: {}", parent.display()))?;
       }

       fs::copy(from, to).with_context(|| {format!("Failed to copy file from {} to {}", from.display(), to.display())})?;

       Ok(())
}

fn copy_all_dlls(from: &Path, to: &Path) -> Result<()> {
       for entry in fs::read_dir(from).with_context(|| anyhow!("Failed to read runtime folder: {}", from.display()))?{
              let entry = entry?;
              let source_path = entry.path();

              if !source_path.is_file() { continue; }

              let Some(extension) = source_path.extension() else { continue;};

              if !extension.to_string_lossy().eq_ignore_ascii_case("dll") {continue;};

              let file_name = source_path.file_name().ok_or_else(|| anyhow!("DLL file has no file name: {}", source_path.display()))?;
              let destination = to.join(file_name);

              copy_file(&source_path, &destination)?;

              println!("Coped DLLs from {} to {}", source_path.display(), destination.display());
       }

       Ok(())
}

fn main() -> anyhow::Result<()> {
       let args: Vec<String> = env::args().collect();

       if args.len() != 4 { return Err(anyhow!("Usage: TilkyExporter <project_metadata_path> <destination_path> <standalone_exe_path>")); }

       let metadata_path = PathBuf::from(&args[1]);
       let destination_path = PathBuf::from(&args[2]);
       let standalone_exe_path = PathBuf::from(&args[3]);

       let project_dir = metadata_path.parent().ok_or_else(|| anyhow!("Could not get project directory from metadata path"))?;
       let standalone_dir = standalone_exe_path.parent().ok_or_else(|| anyhow!("Could not get Standalone.exe directory"))?;

       // This copies:
       // Project/Assets/*
       // into:
       // Destination/Assets/*
       let assets_source = project_dir.join("Assets");
       let assets_destination = destination_path.join("Assets");
       copy_dir_recursive(&assets_source, &assets_destination)?;
       println!("Copied assets from {} to {}", assets_source.display(), assets_destination.display());

       // Copy:
       // StandaloneDir/EngineAssets/Fonts
       // into:
       // Destination/EngineAssets/Fonts
       let engine_fonts_source = standalone_dir.join("EngineAssets").join("Fonts");
       let engine_fonts_destination = destination_path.join("EngineAssets").join("Fonts");
       copy_dir_recursive(&engine_fonts_source, &engine_fonts_destination)?;
       println!("Copied engine fonts from {} to {}", engine_fonts_source.display(), engine_fonts_destination.display());

       // Copy:
       // StandaloneDir/Shaders
       // into:
       // Destination/Shaders
       let shaders_source = standalone_dir.join("Shaders");
       let shaders_destination = destination_path.join("Shaders");
       copy_dir_recursive(&shaders_source, &shaders_destination)?;
       println!("Copied shaders from {} to {}", shaders_source.display(), shaders_destination.display());

       // Copy DLLs and Standalone.exe into the destination root.
       // These will be placed next to Assets/, EngineAssets/, and Shaders/.
       copy_all_dlls(standalone_dir, &destination_path)?;

       // Copy Standalone.exe itself.
       //todo make it so the .exe name is the same as the game name
       let standalone_destination = destination_path.join("Standalone.exe");
       copy_file(&standalone_exe_path, &standalone_destination)?;
       println!("Copied standalone executable from {} to {}", standalone_exe_path.display(), standalone_destination.display());

       println!("Export completed successfully.");

       Ok(())
}