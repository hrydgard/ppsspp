use std::io;

mod section;

mod inifile;
use inifile::IniFile;

use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(subcommand)]
    cmd: Command,
}

#[derive(StructOpt, Debug)]
enum Command {
    CopyMissingLines {},
    CommentUnknownLines {},
    RemoveUnknownLines {},
    MoveKey { old: String, new: String, key: String },
}

fn copy_missing_lines(reference_ini: &IniFile, target_ini: &mut IniFile) -> io::Result<()> {
    // Insert any missing full sections.
    for reference_section in &reference_ini.sections {
        if !target_ini.insert_section_if_missing(reference_section) {
            if let Some(target_section) = target_ini.get_section_mut(&reference_section.name) {
                for line in &reference_section.lines {
                    target_section.insert_line_if_missing(line);
                }

                //target_section.remove_lines_if_not_in(reference_section);
                target_section.comment_out_lines_if_not_in(reference_section);
            }
        }
    }
    Ok(())
}

fn deal_with_unknown_lines(
    reference_ini: &IniFile,
    target_ini: &mut IniFile,
    remove: bool,
) -> io::Result<()> {
    // Insert any missing full sections.
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

fn move_key(
    target_ini: &mut IniFile,
    old: &str,
    new: &str,
    key: &str,
) -> io::Result<()> {
    // Insert any missing full sections.
    if let Some(old_section) = target_ini.get_section_mut(old) {
        if let Some(line) = old_section.remove_line(key) {
            if let Some(new_section) = target_ini.get_section_mut(new) {
                new_section.insert_line_if_missing(&line);
            } else {
                println!("No new section {}", new);
            }
        } else {
            println!("No key {} in section {}", key, old);
        }
    } else {
        println!("No old section {}", old);
    }
    Ok(())
}

fn main() {
    let opt = Opt::from_args();

    // TODO: Grab extra arguments from opt somehow.
    let args: Vec<String> = vec![]; //std::env::args().skip(1).collect();
    let mut filenames = args;

    let root = "../../assets/lang";
    let reference_file = "en_US.ini";

    let reference_ini = IniFile::parse(&format!("{}/{}", root, reference_file)).unwrap();

    if filenames.is_empty() {
        // Grab them all.
        for path in std::fs::read_dir(root).unwrap() {
            let path = path.unwrap();
            if path.file_name() == reference_file {
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

    for filename in filenames {
        if filename == "langtool" {
            // Get this from cargo run for some reason.
            continue;
        }
        let target_ini_filename = format!("{}/{}", root, filename);
        println!("Langtool processing {}", target_ini_filename);

        let mut target_ini = IniFile::parse(&target_ini_filename).unwrap();

        match opt.cmd {
            Command::CopyMissingLines {} => {
                copy_missing_lines(&reference_ini, &mut target_ini).unwrap();
            }
            Command::CommentUnknownLines {} => {
                deal_with_unknown_lines(&reference_ini, &mut target_ini, false).unwrap();
            }
            Command::RemoveUnknownLines {} => {
                deal_with_unknown_lines(&reference_ini, &mut target_ini, true).unwrap();
            }
            Command::MoveKey { ref old, ref new,ref key,  } => {
                move_key(&mut target_ini,  &old, &new, &key).unwrap();
            }
        }

        target_ini.write().unwrap();
    }

    // println!("{:#?}", target_ini);
}
