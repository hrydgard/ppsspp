use std::io;

use std::collections::BTreeMap;

mod section;
use section::{Section, line_value};

mod inifile;
use inifile::IniFile;

mod chatgpt;
use clap::Parser;

#[derive(Parser, Debug)]
struct Args {
    #[command(subcommand)]
    cmd: Command,
    #[arg(short, long)]
    dry_run: bool,
    #[arg(short, long)]
    verbose: bool,
    // gpt-5, gpt-5-mini, gpt-5-nano, gpt-4.1, gpt-4.1-mini,  gpt-4.1-nano, o3, o4-mini, gpt-4o, gpt-4o-realtime-preview
    #[arg(short, long, default_value = "gpt-4o-mini")]
    model: String,
}

#[derive(Parser, Debug)]
enum Command {
    CopyMissingLines {
        #[arg(short, long)]
        dont_comment_missing: bool,
    },
    CommentUnknownLines {},
    RemoveUnknownLines {},
    AddNewKey {
        section: String,
        key: String,
    },
    AddNewKeyAI {
        section: String,
        key: String,
        extra: Option<String>,
    },
    AddNewKeyValue {
        section: String,
        key: String,
        value: String,
    },
    MoveKey {
        old: String,
        new: String,
        key: String,
    },
    CopyKey {
        old: String,
        new: String,
        key: String,
    },
    DupeKey {
        section: String,
        old: String,
        new: String,
    },
    RenameKey {
        section: String,
        old: String,
        new: String,
    },
    SortSection {
        section: String,
    },
    RemoveKey {
        section: String,
        key: String,
    },
    GetNewKeys,
    ImportSingle {
        filename: String,
        section: String,
        key: String,
    },
}

fn copy_missing_lines(
    reference_ini: &IniFile,
    target_ini: &mut IniFile,
    comment_missing: bool,
) -> io::Result<()> {
    for reference_section in &reference_ini.sections {
        // Insert any missing full sections.
        if !target_ini.insert_section_if_missing(reference_section) {
            if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
                for line in &reference_section.lines {
                    target_section.insert_line_if_missing(line);
                }

                //target_section.remove_lines_if_not_in(reference_section);
                if comment_missing {
                    target_section.comment_out_lines_if_not_in(reference_section);
                }
            }
        } else {
            // Note: insert_section_if_missing will copy the entire section,
            // no need to loop over the lines here.
            println!("Inserted missing section: {}", reference_section.name);
        }
    }
    Ok(())
}

fn deal_with_unknown_lines(
    reference_ini: &IniFile,
    target_ini: &mut IniFile,
    remove: bool,
) -> io::Result<()> {
    for reference_section in &reference_ini.sections {
        if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
            if remove {
                target_section.remove_lines_if_not_in(reference_section);
            } else {
                target_section.comment_out_lines_if_not_in(reference_section);
            }
        }
    }
    Ok(())
}

fn print_keys_if_not_in(
    reference_ini: &IniFile,
    target_ini: &mut IniFile,
    header: &str,
) -> io::Result<()> {
    for reference_section in &reference_ini.sections {
        if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
            let keys = target_section.get_keys_if_not_in(reference_section);
            if !keys.is_empty() {
                println!("{} ({})", reference_section.name, header);
                for key in &keys {
                    println!("- {key}");
                }
            }
        }
    }
    Ok(())
}

fn move_key(target_ini: &mut IniFile, old: &str, new: &str, key: &str) -> io::Result<()> {
    if let Some(old_section) = target_ini.get_section_mut(old) {
        if let Some(line) = old_section.remove_line(key) {
            if let Some(new_section) = target_ini.get_section_mut(new) {
                new_section.insert_line_if_missing(&line);
            } else {
                println!("No new section {new}");
            }
        } else {
            println!("No key {key} in section {old}");
        }
    } else {
        println!("No old section {old}");
    }
    Ok(())
}

fn copy_key(target_ini: &mut IniFile, old: &str, new: &str, key: &str) -> io::Result<()> {
    if let Some(old_section) = target_ini.get_section_mut(old) {
        if let Some(line) = old_section.get_line(key) {
            if let Some(new_section) = target_ini.get_section_mut(new) {
                new_section.insert_line_if_missing(&line);
            } else {
                println!("No new section {new}");
            }
        } else {
            println!("No key {key} in section {old}");
        }
    } else {
        println!("No old section {old}");
    }
    Ok(())
}

fn remove_key(target_ini: &mut IniFile, section: &str, key: &str) -> io::Result<()> {
    if let Some(old_section) = target_ini.get_section_mut(section) {
        old_section.remove_line(key);
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn add_new_key(target_ini: &mut IniFile, section: &str, key: &str, value: &str) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        section.insert_line_if_missing(&format!("{key} = {value}"));
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn rename_key(target_ini: &mut IniFile, section: &str, old: &str, new: &str) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        section.rename_key(old, new);
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn dupe_key(target_ini: &mut IniFile, section: &str, old: &str, new: &str) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        section.dupe_key(old, new);
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn sort_section(target_ini: &mut IniFile, section: &str) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        section.sort();
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn generate_prompt(filenames: &[String], section: &str, value: &str, extra: &str) -> String {
    let languages = filenames
        .iter()
        .map(|filename| filename.split_once(".").unwrap().0)
        .collect::<Vec<&str>>()
        .join(", ");

    let base_str = format!("Please translate '{value}' from US English to all of these languages: {languages}.
    Output in json format, a single dictionary, key=value. Include en_US first (the original string).
    For context, the string will be in the translation section '{section}', and these strings are UI strings for my PSP emulator application.
    'frame' refers to a displayed image frame of the running game, not a photo frame. In Swedish, frame is best translated as bildruta, and similar
    may apply in other Nordic languages. Keep the strings relatively short, don't let them become more than 40% longer than the original string.
    Do not output any text before or after the list of translated strings, do not ask followups.
    {extra}");

    base_str
}

fn parse_response(response: &str) -> Option<BTreeMap<String, String>> {
    // Try to find JSON in the response (it might have other text around it)
    let response = response.trim();

    // Look for JSON object boundaries
    let start = response.find('{')?;
    let end = response.rfind('}')? + 1;
    let json_str = &response[start..end];

    // Parse the JSON into a BTreeMap
    match serde_json::from_str::<BTreeMap<String, serde_json::Value>>(json_str) {
        Ok(json_map) => {
            let mut result = BTreeMap::new();
            for (key, value) in json_map {
                // Convert JSON values to strings
                let string_value = match value {
                    serde_json::Value::String(s) => s,
                    _ => value.to_string().trim_matches('"').to_string(),
                };
                result.insert(key, string_value);
            }
            Some(result)
        }
        Err(e) => {
            eprintln!("Failed to parse JSON response: {}", e);
            eprintln!("JSON string was: {}", json_str);
            None
        }
    }
}

// TODO: Look into using https://github.com/Byron/google-apis-rs/tree/main/gen/translate2 for initial translations.

fn main() {
    let opt = Args::parse();

    let api_key = std::env::var("OPENAI_API_KEY").ok();

    let ai = api_key.map(|key| chatgpt::ChatGPT::new(key, opt.model));

    // TODO: Grab extra arguments from opt somehow.
    let args: Vec<String> = vec![]; //std::env::args().skip(1).collect();
    let mut filenames = args;

    let root = "../../assets/lang";
    let reference_ini_filename = "en_US.ini";

    let mut reference_ini = IniFile::parse(&format!("{root}/{reference_ini_filename}")).unwrap();

    if filenames.is_empty() {
        // Grab them all.
        for path in std::fs::read_dir(root).unwrap() {
            let path = path.unwrap();
            if path.file_name() == reference_ini_filename {
                continue;
            }
            let filename = path.file_name();
            let filename = filename.to_string_lossy();
            if !filename.ends_with(".ini") {
                continue;
            }
            filenames.push(path.file_name().to_string_lossy().to_string());
        }
    }

    let mut single_ini_section: Option<Section> = None;
    if let Command::ImportSingle {
        filename,
        section,
        key: _,
    } = &opt.cmd
    {
        if let Ok(single_ini) = IniFile::parse(filename) {
            if let Some(single_section) = single_ini.get_section("Single") {
                single_ini_section = Some(single_section.clone());
            } else {
                println!("No section {section} in {filename}");
            }
        } else {
            println!("Failed to parse {filename}");
            return;
        }
    }

    let ai_response = if let Command::AddNewKeyAI {
        section,
        key,
        extra,
    } = &opt.cmd
    {
        let prompt = generate_prompt(
            &filenames,
            section,
            key,
            &extra.clone().unwrap_or("".to_string()),
        );
        println!("generated prompt:\n{prompt}");
        if let Some(ai) = &ai {
            println!("Using AI for translation...");
            let response = ai
                .chat(&prompt)
                .map_err(|e| anyhow::anyhow!("chat failed: {e}"))
                .unwrap();
            println!("AI response: {response}");
            if let Some(parsed) = parse_response(&response) {
                println!("Parsed: {:?}", parsed);

                if parsed.len() < filenames.len() {
                    println!(
                        "Not enough languages generated! {} vs {}",
                        parsed.len(),
                        filenames.len()
                    );
                }

                Some(parsed)
            } else {
                println!("Failed to parse AI response, not doing anything.");
                return;
            }
        } else {
            println!("AI key not set, skipping AI command.");
            return;
        }
    } else {
        None
    };

    for filename in &filenames {
        let reference_ini = &reference_ini;
        if filename == "langtool" {
            // Get this from cargo run for some reason.
            continue;
        }
        let target_ini_filename = format!("{root}/{filename}");
        if opt.verbose {
            println!("Langtool processing {target_ini_filename}");
        }

        let mut target_ini = IniFile::parse(&target_ini_filename).unwrap();

        match opt.cmd {
            Command::CopyMissingLines {
                dont_comment_missing,
            } => {
                copy_missing_lines(reference_ini, &mut target_ini, !dont_comment_missing).unwrap();
            }
            Command::CommentUnknownLines {} => {
                deal_with_unknown_lines(reference_ini, &mut target_ini, false).unwrap();
            }
            Command::RemoveUnknownLines {} => {
                deal_with_unknown_lines(reference_ini, &mut target_ini, true).unwrap();
            }
            Command::GetNewKeys => {
                print_keys_if_not_in(reference_ini, &mut target_ini, &target_ini_filename).unwrap();
            }
            Command::SortSection { ref section } => sort_section(&mut target_ini, section).unwrap(),
            Command::RenameKey {
                ref section,
                ref old,
                ref new,
            } => rename_key(&mut target_ini, section, old, new).unwrap(),
            Command::AddNewKey {
                ref section,
                ref key,
            } => add_new_key(&mut target_ini, section, key, key).unwrap(),
            Command::AddNewKeyAI {
                ref section,
                ref key,
                extra: _,
            } => {
                let lang = filename.split_once('.').unwrap().0;
                if let Some(ai_response) = &ai_response {
                    // Process it.
                    if let Some(translated_string) = ai_response.get(lang) {
                        println!("{lang}:");
                        add_new_key(&mut target_ini, section, key, translated_string).unwrap();
                    } else {
                        println!("Language {lang} not found in response. Bailing.");
                        return;
                    }
                }
            }
            Command::AddNewKeyValue {
                ref section,
                ref key,
                ref value,
            } => add_new_key(&mut target_ini, section, key, value).unwrap(),
            Command::MoveKey {
                ref old,
                ref new,
                ref key,
            } => {
                move_key(&mut target_ini, old, new, key).unwrap();
            }
            Command::CopyKey {
                // Copies between sections
                ref old,
                ref new,
                ref key,
            } => {
                copy_key(&mut target_ini, old, new, key).unwrap();
            }
            Command::DupeKey {
                ref section,
                ref old,
                ref new,
            } => {
                dupe_key(&mut target_ini, section, old, new).unwrap();
            }
            Command::RemoveKey {
                ref section,
                ref key,
            } => {
                remove_key(&mut target_ini, section, key).unwrap();
            }
            Command::ImportSingle {
                filename: _,
                ref section,
                ref key,
            } => {
                let lang_id = filename.strip_suffix(".ini").unwrap();
                if let Some(single_section) = &single_ini_section {
                    if let Some(target_section) = target_ini.get_section_mut(section) {
                        if let Some(single_line) = single_section.get_line(lang_id) {
                            if let Some(value) = line_value(&single_line) {
                                println!(
                                    "Inserting value {value} for key {key} in section {section} in {target_ini_filename}"
                                );
                                if !target_section
                                    .insert_line_if_missing(&format!("{key} = {value}"))
                                {
                                    // Didn't insert it, so it exists. We need to replace it.
                                    target_section.set_value(key, value);
                                }
                            }
                        } else {
                            println!("No lang_id {lang_id} in single section");
                        }
                    } else {
                        println!("No section {section} in {target_ini_filename}");
                    }
                } else {
                    println!("No section {section} in {filename}");
                }
            }
        }

        if !opt.dry_run {
            target_ini.write().unwrap();
        }
    }

    println!("Langtool processing {reference_ini_filename}");

    // Some commands also apply to the reference ini.
    match opt.cmd {
        Command::AddNewKey {
            ref section,
            ref key,
        } => {
            add_new_key(&mut reference_ini, section, key, key).unwrap();
        }
        Command::AddNewKeyAI {
            ref section,
            ref key,
            ref extra,
        } => {
            if ai_response.is_some() {
                let _ = extra;
                add_new_key(&mut reference_ini, section, key, key).unwrap();
            }
        }
        Command::AddNewKeyValue {
            ref section,
            ref key,
            ref value,
        } => {
            add_new_key(&mut reference_ini, section, key, value).unwrap();
        }
        Command::SortSection { ref section } => sort_section(&mut reference_ini, section).unwrap(),
        Command::RenameKey {
            ref section,
            ref old,
            ref new,
        } => {
            if old == new {
                println!("WARNING: old == new");
            }
            rename_key(&mut reference_ini, section, old, new).unwrap();
        }
        Command::MoveKey {
            ref old,
            ref new,
            ref key,
        } => {
            move_key(&mut reference_ini, old, new, key).unwrap();
        }
        Command::CopyKey {
            // between sections
            ref old,
            ref new,
            ref key,
        } => {
            copy_key(&mut reference_ini, old, new, key).unwrap();
        }
        Command::DupeKey {
            // Inside a section, preserving a value
            ref section,
            ref old,
            ref new,
        } => {
            dupe_key(&mut reference_ini, section, old, new).unwrap();
        }
        Command::RemoveKey {
            ref section,
            ref key,
        } => {
            remove_key(&mut reference_ini, section, key).unwrap();
        }
        _ => {}
    }

    if !opt.dry_run {
        reference_ini.write().unwrap();
    }
}
