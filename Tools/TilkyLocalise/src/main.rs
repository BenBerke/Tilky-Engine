use anyhow::{anyhow, Result};
use serde_json::{json, Value};
use std::path::PathBuf;
use std::{env, fs, io};

fn get_local_dir() -> Result<PathBuf> {
    // This points to: ...\Wolfy Engine\Tools\TilkyLocalise
    let project_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));

    let local_dir = project_dir
        .parent()
        .and_then(|p| p.parent()) // up to \Wolfy Engine
        .map(|p| p.join("EngineAssets").join("Local"))
        .ok_or_else(|| anyhow!("Could not resolve the parent asset directory"))?;

    if !local_dir.exists() {
        fs::create_dir_all(&local_dir)?;
    }

    Ok(local_dir)
}

fn main() -> Result<()> {
    let local_dir = get_local_dir()?;

    let mut first_input = String::new();
    let mut second_input = String::new();
    let mut language_names = Vec::new();

    println!("Existing files found in {:?}:", local_dir);
    for entry in fs::read_dir(&local_dir)? {
        let entry = entry?;
        let path = entry.path();

        if path.is_file() && path.extension().is_some_and(|ext| ext == "json"){
            let language_name = path.file_stem().unwrap().to_string_lossy().to_string();
            let file = fs::read_to_string(&path)?;

            if let Ok(file_json) = serde_json::from_str::<Value>(&file) {
                language_names.push(language_name.clone());
                let lang = file_json.get("language.name")
                    .map(|v| v.as_str().unwrap_or("Unknown"))
                    .unwrap_or("No Name Key");

                println!("  * {} : {}", lang, language_name);
            }
        }
    }

    println!("\nSelect the base language by typing the value on the right (\"en\" for English is suggested)");
    io::stdin().read_line(&mut first_input)?;
    let first = first_input.trim_matches(|c: char| c.is_whitespace() || c == '\r' || c == '\n');

    println!("Select the target language you want to translate to");
    io::stdin().read_line(&mut second_input)?;
    let second = second_input.trim_matches(|c: char| c.is_whitespace() || c == '\r' || c == '\n');

    if first == second {
        println!("Base and target can not be the same");
        return Ok(());
    }

    let found_base = language_names.iter().any(|name| name == first);
    let found_target = language_names.iter().any(|name| name == second);

    let base: String;
    let target: String;

    if found_base {
        let base_path = local_dir.join(format!("{}.json", first));
        base = fs::read_to_string(&base_path)
            .map_err(|e| anyhow!("Failed to read base file {:?}: {}", base_path, e))?;
    } else {
        println!("Base '{}' could not be found, falling back to en.json", first);
        let en_path = local_dir.join("en.json");
        base = fs::read_to_string(&en_path)
            .map_err(|e| anyhow!("Fallback 'en.json' missing at {:?}: {}", en_path, e))?;
    }

    if found_target {
        let target_path = local_dir.join(format!("{}.json", second));
        target = fs::read_to_string(target_path)?;
    } else {
        println!("Target '{}' could not be found, creating new file", second);
        target = "{}".to_string();
    }

    let base_json: Value = serde_json::from_str(&base)?;
    let mut target_json: Value = serde_json::from_str(&target)?;

    let base_object = base_json.as_object().ok_or_else(|| anyhow!("Base JSON is not an object"))?;
    let target_object = target_json.as_object_mut().ok_or_else(|| anyhow!("Target JSON is not an object"))?;

    for key in base_object.keys() {
        if !target_object.contains_key(key) {
            target_object.insert(key.to_string(), json!("$Missing Translation"));
        }
    }

    let output = serde_json::to_string_pretty(&target_json)?;
    let destination_path = local_dir.join(format!("{}.json", second));
    fs::write(&destination_path, output)?;

    println!("Successfully processed and updated target file!");
    let mut wait = String::new();
    io::stdin().read_line(&mut wait)?;
    Ok(())
}