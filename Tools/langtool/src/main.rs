use std::io;

use std::collections::BTreeMap;

mod section;
use section::{Section, line_value};

mod inifile;
use inifile::IniFile;

mod chatgpt;
use clap::Parser;

mod util;

use crate::{chatgpt::ChatGPT, section::split_line, util::ask_letter};

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
    ListUnknownLines {},
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
        #[arg(short, long)]
        overwrite_translated: bool,
    },
    AddNewKeyValueAI {
        section: String,
        key: String,
        value: String,
        extra: Option<String>,
        #[arg(short, long)]
        overwrite_translated: bool,
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
        old_section: String,
        new_section: String,
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
    CheckRefKeys,
    FixupRefKeys, // This was too big a job.
    FinishLanguageWithAI {
        language: String,
        section: Option<String>,
    },
    RemoveLinebreaks {
        section: String,
        key: String,
    },
    ApplyRegex {
        section: String,
        key: String,
        pattern: String,
        replacement: Option<String>,
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

enum UnknownLineAction {
    Remove,
    Comment,
    Log,
}

fn deal_with_unknown_lines(
    reference_ini: &IniFile,
    target_ini: &mut IniFile,
    action: UnknownLineAction,
) -> io::Result<()> {
    for reference_section in &reference_ini.sections {
        if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
            match action {
                UnknownLineAction::Remove => {
                    target_section.remove_lines_if_not_in(reference_section)
                }
                UnknownLineAction::Comment => {
                    target_section.comment_out_lines_if_not_in(reference_section)
                }
                UnknownLineAction::Log => {
                    let unknown_lines = target_section.get_lines_not_in(reference_section);
                    if !unknown_lines.is_empty() {
                        println!("Unknown lines in section [{}]:", target_section.name);
                        for line in unknown_lines {
                            println!("  {}", line);
                        }
                    }
                }
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

fn remove_linebreaks(target_ini: &mut IniFile, section: &str, key: &str) -> io::Result<()> {
    if let Some(old_section) = target_ini.get_section_mut(section) {
        old_section.remove_linebreaks(key);
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn add_new_key(
    target_ini: &mut IniFile,
    section: &str,
    key: &str,
    value: &str,
    overwrite_translated: bool,
) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        if !overwrite_translated {
            if let Some(existing_value) = section.get_value(key) && existing_value != key {
                // This one was already translated. Skip it.
                println!(
                    "Key '{key}' already has a translated value '{existing_value}', skipping."
                );
                return Ok(());
            }
        }
        section.insert_line_if_missing(&format!("{key} = {value}"));
    } else {
        println!("No section {section}");
    }
    Ok(())
}

fn check_keys(target_ini: &IniFile) -> io::Result<()> {
    for section in &target_ini.sections {
        let mut mismatches = Vec::new();

        if section.name == "DesktopUI" {
            // ignore the ampersands for now
            continue;
        }

        for line in &section.lines {
            if let Some((key, value)) = split_line(line) {
                if key != value {
                    mismatches.push((key, value));
                }
            }
        }

        if !mismatches.is_empty() {
            println!("[{}]", section.name);
            for (key, value) in mismatches {
                println!("  {key} != {value}");
            }
        }
    }
    Ok(())
}

fn fixup_keys(target_ini: IniFile, dry_run: bool) -> io::Result<()> {
    for section in &target_ini.sections {
        let mut mismatches = Vec::new();

        if section.name == "DesktopUI"
            || section.name == "MappableControls"
            || section.name == "PostShaders"
        {
            // ignore the ampersands for now, also mappable controls, we don't want to change those strings.
            continue;
        }

        for line in &section.lines {
            if let Some((key, value)) = split_line(line) && key != value {
                mismatches.push((key, value));
            }
        }

        if !mismatches.is_empty() {
            println!("[{}]", section.name);
            for (key, value) in mismatches {
                if (key.len() as i32 - value.len() as i32).abs() > 15 {
                    println!("  (skipping {key} = {value} (probably an alias))");
                    continue;
                }
                if value.contains(r"\n") {
                    println!("  (skipping {key} = {value} (line break))");
                    continue;
                }
                if value.contains("×") || value.contains("\"") {
                    println!("  (skipping {key} = {value} (symbol))");
                    continue;
                }
                if key.contains("ardboard") {
                    println!("  (skipping {key} = {value} (cardboard))");
                    continue;
                }
                if key.contains("translators") {
                    continue;
                }

                let _ = cli_clipboard::set_contents(format!("\"{key}\""));

                match ask_letter(&format!("  '{key}' != '{value}' ? >\n"), "ynrd") {
                    'y' => execute_command(
                        Command::RenameKey {
                            section: section.name.clone(),
                            old: key.to_string(),
                            new: value.to_string(),
                        },
                        None,
                        dry_run,
                        false,
                    ),
                    'r' => {
                        println!("reverse fixup not supported yet");
                    }
                    'q' => {
                        println!("Cancelled! Quitting.");
                        return Ok(());
                    }
                    'd' => execute_command(
                        Command::RemoveKey {
                            section: section.name.clone(),
                            key: key.to_string(),
                        },
                        None,
                        dry_run,
                        false,
                    ),

                    'n' => {}
                    _ => {
                        println!("Invalid response, ignoring.");
                    }
                }
            }
        }
    }
    Ok(())
}

fn finish_language_with_ai(
    target_language: &str,
    target_ini: &mut IniFile,
    ref_ini: &IniFile,
    section: Option<&str>,
    ai: &ChatGPT,
    dry_run: bool,
) -> anyhow::Result<()> {
    println!("Finishing language with AI");
    println!(
        "Step 1: Compare all strings in the section with the matching strings from the reference."
    );

    let sections: Vec<Section> = if let Some(section_name) = section {
        vec![target_ini.get_section(section_name).unwrap().clone()]
    } else {
        target_ini.sections.to_vec()
    };

    let base_prompt = format!(
        "Please translate the below list of strings from US English to {target_language}.
    After the strings to translate, there are related already-translated strings that may help for context.
    Note that the strings are UI strings for my PSP emulator application.
    Also, please output similarly to the input, with section headers and key=value pairs. The section name
    is not to be translated.

    Here are the strings to translate:
    "
    );
    let suffix = "    Do not output any text before or after the list of translated strings, do not ask followups.
    'undo state' means a saved state that's been saved so that the last save state operation can be undone.
    DO NOT translate strings like DDMMYYYY, MMDDYYYY and similar technical letters and designations. Not even
    translating the individual letters, they need to be kept as-is.
    'JIT using IR' should be interpreted as 'JIT, with IR'.
    Don't translate strings about 'load undo state' or 'save undo state', also not about savestate slots.
    IMPORTANT! 'Notification screen position' means the position on the screen where notifications are displayed,
    not the position of a 'notification screen', no such thing.
    %1 is a placeholder for a number or word, do not change it, just make sure it ends up in the right location.
    A 'driver manager' is a built-in tool to manage drivers, not a human boss. Same goes for other types of manager.
    The '=' at the end of the lines to translate is not part of the translation keys.
    ";

    for section in sections {
        let Some(ref_section) = ref_ini.get_section(&section.name) else {
            println!("Section '{}' not found in reference file", section.name);
            continue;
        };
        let mut alias_map = BTreeMap::new();
        let mut alias_inverse_map = BTreeMap::new();
        for line in &ref_section.lines {
            if let Some((key, value)) = split_line(line) {
                // We actually process almost everything here, we could check for case but we don't
                // since the aliased case is better.
                if key != value {
                    println!("Saving alias: {key} = {value}");
                    alias_map.insert(key, value.to_string());
                    alias_inverse_map.insert(value.to_string(), key);
                }
            }
        }

        // When just testing aliases.
        // return Ok(());

        let mut untranslated_keys = vec![];
        let mut translated_keys = vec![];
        for line in &section.lines {
            if let Some((key, value)) = split_line(line) {
                if let Some(ref_value) = ref_section.get_value(key) {
                    if value == ref_value {
                        // Key not translated.
                        // However, we need to reject some things that the AI likes to mishandle.
                        if value.to_uppercase() == value {
                            println!(
                                "Skipping untranslated key '{}' with uppercase value '{}'",
                                key, value
                            );
                            continue;
                        }
                        untranslated_keys.push((key, ref_value));
                    } else {
                        translated_keys.push((key, value));
                    }
                } else {
                    println!(
                        "Key '{}' not found in reference section '{}'",
                        key, ref_section.name
                    );
                }
            }
        }

        println!(
            "[{}]: Found {} untranslated keys",
            section.name,
            untranslated_keys.len()
        );
        if untranslated_keys.is_empty() {
            continue;
        }

        for (key, ref_value) in &untranslated_keys {
            println!(" - '{} (ref: '{}')", key, ref_value);
        }

        // Here you would call the AI to translate the keys.
        let section_prompt = format!(
            "{base_prompt}\n\n[{}]\n{}\n\n\n\nBelow are the already translated strings for context, don't re-translate these:\n\n{}\n\n{}",
            section.name,
            untranslated_keys
                .iter()
                .map(|(k, _v)| format!("{} = ", alias_map.get(k).unwrap_or(&k.to_string())))
                .collect::<Vec<String>>()
                .join("\n"),
            translated_keys
                .iter()
                .map(|(k, v)| format!("{} = {}", k, v))
                .collect::<Vec<String>>()
                .join("\n"),
            suffix
        );
        println!("[{}] AI prompt:\n{}", section.name, section_prompt);
        if !dry_run {
            println!("Running AI translation...");
            let response = ai
                .chat(&section_prompt)
                .map_err(|e| anyhow::anyhow!("chat failed: {e}"))?;
            println!("AI response:\n{}", response);
            // Now we just need to merge the AI response into the target_ini.
            let parsed_response = IniFile::parse_string(&response)
                .map_err(|e| anyhow::anyhow!("Failed to parse AI response: {e}"))?;
            if parsed_response.sections.is_empty() {
                println!("No sections found in AI response! bad!");
            }
            let target_section = target_ini.get_section_mut(&section.name).unwrap();
            for parsed_section in parsed_response.sections {
                if parsed_section.name == section.name {
                    println!("Merging AI response for section '{}'", parsed_section.name);
                    for line in &parsed_section.lines {
                        if let Some((key, value)) = split_line(line) {
                            // Put the key through the inverse alias map.
                            let original_key = alias_inverse_map.get(key).unwrap_or(&key);
                            print!("Updating '{}': {}", original_key, value);
                            if key != *original_key {
                                println!(" ({})", key);
                            } else {
                                println!();
                            }
                            if !target_section.set_value(
                                original_key,
                                value,
                                Some("AI translated"),
                            ) {
                                println!("Failed to update '{}'", original_key);
                            }
                        }
                    }
                } else {
                    println!("Mismatched section name '{}'", parsed_section.name);
                }
            }
        }
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

fn apply_regex(
    target_ini: &mut IniFile,
    section: &str,
    key: &str,
    pattern: &str,
    replacement: &str,
) -> io::Result<()> {
    if let Some(section) = target_ini.get_section_mut(section) {
        section.apply_regex(key, pattern, replacement);
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
    Keep the strings relatively short, don't let them become more than 40% longer than the original string.
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

fn main() {
    let opt = Args::parse();

    let api_key = std::env::var("OPENAI_API_KEY").ok();
    let ai = api_key.map(|key| chatgpt::ChatGPT::new(key, opt.model));

    // TODO: Grab extra arguments from opt somehow.
    // let args: Vec<String> = vec![]; //std::env::args().skip(1).collect();
    execute_command(opt.cmd, ai.as_ref(), opt.dry_run, opt.verbose);
}

fn execute_command(cmd: Command, ai: Option<&ChatGPT>, dry_run: bool, verbose: bool) {
    let root = "../../assets/lang";
    let reference_ini_filename = "en_US.ini";

    let mut reference_ini =
        IniFile::parse_file(&format!("{root}/{reference_ini_filename}")).unwrap();

    let mut filenames = Vec::new();
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
    } = &cmd
    {
        if let Ok(single_ini) = IniFile::parse_file(filename) {
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

    if let Command::FinishLanguageWithAI { language, section } = &cmd {
        if let Some(ai) = &ai {
            let target_ini_filename = format!("{root}/{language}.ini");
            let mut target_ini = IniFile::parse_file(&target_ini_filename).unwrap();
            finish_language_with_ai(
                language,
                &mut target_ini,
                &reference_ini,
                section.as_deref(),
                ai,
                dry_run,
            )
            .unwrap();
            if !dry_run {
                println!("Writing modified file for target language: {}", language);
                target_ini.write().unwrap();
            }
        } else {
            println!("FinishLanguageWithAI: AI key not set, skipping AI command.");
        }
        return;
    }

    // This is a bit ugly, but we need to generate the AI response before processing files.
    let ai_response = if let Command::AddNewKeyAI {
        section,
        key,
        extra,
        overwrite_translated: _,
    } = &cmd
    {
        match generate_ai_response(ai, &filenames, section, key, extra) {
            Some(value) => value,
            None => return,
        }
    } else if let Command::AddNewKeyValueAI {
        section,
        key: _, // We don't need the key here, it's used later when writing to the ini file.
        value,
        extra,
        overwrite_translated: _,
    } = &cmd
    {
        match generate_ai_response(ai, &filenames, section, value, extra) {
            Some(value) => value,
            None => return,
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
        if verbose {
            println!("Langtool processing {target_ini_filename}");
        }

        let mut target_ini = IniFile::parse_file(&target_ini_filename).unwrap();

        match cmd {
            Command::ApplyRegex {
                ref section,
                ref key,
                ref pattern,
                ref replacement,
            } => {
                apply_regex(
                    &mut target_ini,
                    section,
                    key,
                    pattern,
                    replacement.as_ref().unwrap_or(&"".to_string()),
                )
                .unwrap();
            }
            Command::FinishLanguageWithAI {
                language: _,
                section: _,
            } => {}
            Command::FixupRefKeys => {}
            Command::CheckRefKeys => {}
            Command::CopyMissingLines {
                dont_comment_missing,
            } => {
                copy_missing_lines(reference_ini, &mut target_ini, !dont_comment_missing).unwrap();
            }
            Command::CommentUnknownLines {} => {
                deal_with_unknown_lines(reference_ini, &mut target_ini, UnknownLineAction::Comment)
                    .unwrap();
            }
            Command::RemoveUnknownLines {} => {
                deal_with_unknown_lines(reference_ini, &mut target_ini, UnknownLineAction::Remove)
                    .unwrap();
            }
            Command::ListUnknownLines {} => {
                deal_with_unknown_lines(reference_ini, &mut target_ini, UnknownLineAction::Log)
                    .unwrap();
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
            } => add_new_key(&mut target_ini, section, key, key, false).unwrap(),
            Command::AddNewKeyAI {
                ref section,
                ref key,
                extra: _,
                overwrite_translated,
            } => {
                let lang = filename.split_once('.').unwrap().0;
                if let Some(ai_response) = &ai_response {
                    // Process it.
                    if let Some(translated_string) = ai_response.get(lang) {
                        println!("{lang}:");
                        add_new_key(
                            &mut target_ini,
                            section,
                            key,
                            &format!("{translated_string} # AI translated"),
                            overwrite_translated,
                        )
                        .unwrap();
                    } else {
                        println!("Language {lang} not found in response. Bailing.");
                        return;
                    }
                }
            }
            Command::AddNewKeyValueAI {
                ref section,
                ref key,
                value: _,  // was translated above
                extra: _,
                overwrite_translated,
            } => {
                let lang = filename.split_once('.').unwrap().0;
                if let Some(ai_response) = &ai_response {
                    // Process it.
                    if let Some(translated_string) = ai_response.get(lang) {
                        println!("{lang}:");
                        add_new_key(
                            &mut target_ini,
                            section,
                            key,
                            &format!("{translated_string} # AI translated"),
                            overwrite_translated,
                        )
                        .unwrap();
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
            } => add_new_key(&mut target_ini, section, key, value, false).unwrap(),
            Command::MoveKey {
                ref old,
                ref new,
                ref key,
            } => {
                move_key(&mut target_ini, old, new, key).unwrap();
            }
            Command::CopyKey {
                // Copies between sections
                ref old_section,
                ref new_section,
                ref key,
            } => {
                copy_key(&mut target_ini, old_section, new_section, key).unwrap();
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
            Command::RemoveLinebreaks {
                ref section,
                ref key,
            } => {
                remove_linebreaks(&mut target_ini, section, key).unwrap();
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
                                if !target_section.insert_line_if_missing(&format!(
                                    "{key} = {value} # AI translated"
                                )) {
                                    // Didn't insert it, so it exists. We need to replace it.
                                    target_section.set_value(key, value, Some("AI translated"));
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

        if !dry_run {
            target_ini.write().unwrap();
        }
    }

    println!("Langtool processing reference {reference_ini_filename}");

    // Some commands also apply to the reference ini.
    match cmd {
        Command::ApplyRegex {
            ref section,
            ref key,
            ref pattern,
            ref replacement,
        } => {
            apply_regex(
                &mut reference_ini,
                section,
                key,
                pattern,
                replacement.as_ref().unwrap_or(&"".to_string()),
            )
            .unwrap();
        }
        Command::FinishLanguageWithAI {
            language: _,
            section: _,
        } => {}
        Command::CheckRefKeys => check_keys(&reference_ini).unwrap(),
        Command::FixupRefKeys => fixup_keys(reference_ini.clone(), dry_run).unwrap(),
        Command::AddNewKey {
            ref section,
            ref key,
        } => {
            add_new_key(&mut reference_ini, section, key, key, false).unwrap();
        }
        Command::AddNewKeyAI {
            ref section,
            ref key,
            ref extra,
            overwrite_translated,
        } => {
            if ai_response.is_some() {
                let _ = extra;
                add_new_key(&mut reference_ini, section, key, key, overwrite_translated).unwrap();
            }
        }
        Command::AddNewKeyValueAI {
            ref section,
            ref key,
            ref value,
            extra,
            overwrite_translated,
        } => {
            if ai_response.is_some() {
                let _ = extra;
                add_new_key(
                    &mut reference_ini,
                    section,
                    key,
                    value,
                    overwrite_translated,
                )
                .unwrap();
            }
        }
        Command::AddNewKeyValue {
            ref section,
            ref key,
            ref value,
        } => {
            add_new_key(&mut reference_ini, section, key, value, false).unwrap();
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
            ref old_section,
            ref new_section,
            ref key,
        } => {
            copy_key(&mut reference_ini, old_section, new_section, key).unwrap();
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
        Command::RemoveLinebreaks {
            ref section,
            ref key,
        } => {
            remove_linebreaks(&mut reference_ini, section, key).unwrap();
        }
        Command::CopyMissingLines {
            dont_comment_missing: _,
        } => {}
        Command::ListUnknownLines {} => {}
        Command::CommentUnknownLines {} => {}
        Command::RemoveUnknownLines {} => {}
        Command::GetNewKeys => {}
        Command::ImportSingle {
            filename: _,
            section: _,
            key: _,
        } => {}
    }

    if !dry_run {
        reference_ini.write().unwrap();
    }
}

fn generate_ai_response(
    ai: Option<&ChatGPT>,
    filenames: &[String],
    section: &str,
    key: &str,
    extra: &Option<String>,
) -> Option<Option<BTreeMap<String, String>>> {
    let prompt = generate_prompt(
        filenames,
        section,
        key,
        &extra.clone().unwrap_or("".to_string()),
    );
    println!("generated prompt:\n{prompt}");
    Some(if let Some(ai) = &ai {
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
            return None;
        }
    } else {
        println!("AI key not set, skipping AI command.");
        return None;
    })
}
