use std::fs::File;
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use crate::section::Section;

#[derive(Debug)]
pub struct IniFile {
    pub filename: PathBuf,
    pub preamble: Vec<String>,
    pub sections: Vec<Section>,
    pub has_bom: bool,
}

impl IniFile {
    pub fn parse(filename: &str) -> io::Result<IniFile> {
        let lines = read_lines(filename)?;

        let mut sections = vec![];
        let mut preamble = vec![];
        let mut cur_section = None;

        let mut has_bom = false;

        for line in lines {
            let line = line.unwrap();

            let line = if let Some(line) = line.strip_prefix('\u{feff}') {
                has_bom = true;
                line
            } else {
                &line
            };

            if let Some('[') = line.chars().next() {
                if let Some(right_bracket) = line.find(']') {
                    if let Some(section) = cur_section.take() {
                        sections.push(section);
                    }

                    let name = &line[1..right_bracket];
                    cur_section = Some(Section {
                        name: name.to_owned(),
                        title_line: line.to_owned(), // preserves comment and bom
                        lines: vec![],
                    });
                } else {
                    // Bad syntax
                    break;
                }
            } else if let Some(cur_section) = &mut cur_section {
                cur_section.lines.push(line.to_owned());
            } else {
                preamble.push(line.to_owned());
            }
        }

        if let Some(section) = cur_section.take() {
            sections.push(section);
        }

        let ini = IniFile {
            filename: PathBuf::from(filename),
            preamble,
            sections,
            has_bom,
        };
        Ok(ini)
    }

    pub fn write(&self) -> io::Result<()> {
        let file = std::fs::File::create(&self.filename)?;
        let mut file = std::io::LineWriter::new(file);

        // Write BOM
        if self.has_bom {
            file.write_all("\u{feff}".as_bytes())?;
        }
        for line in &self.preamble {
            file.write_all(line.as_bytes())?;
            file.write_all(b"\n")?;
        }
        for section in &self.sections {
            file.write_all(section.title_line.as_bytes())?;
            file.write_all(b"\n")?;
            for line in &section.lines {
                file.write_all(line.as_bytes())?;
                file.write_all(b"\n")?;
            }
        }

        Ok(())
    }

    // Assumes alphabetical section order!
    pub fn insert_section_if_missing(&mut self, section: &Section) -> bool {
        // First, check if it's there.

        for iter_section in &self.sections {
            if iter_section.name == section.name {
                return false;
            }
        }

        // Then, find a suitable insertion spot
        for (i, iter_section) in self.sections.iter_mut().enumerate() {
            if iter_section.name > section.name {
                println!("Inserting section {}", section.name);
                self.sections.insert(i, section.clone());
                return true;
            }
        }
        // Reached the end for some reason? Add it.
        // Also add an empty line to the previous section.
        if let Some(last) = self.sections.last_mut() {
            last.lines.push("".into());
        }
        self.sections.push(section.clone());
        true
    }

    pub fn get_section_mut(&mut self, section_name: &str) -> Option<&mut Section> {
        self.sections
            .iter_mut()
            .find(|section| section.name == section_name)
    }
}

// Grabbed from a sample, a fast line reader iterator.
fn read_lines<P>(filename: P) -> io::Result<io::Lines<io::BufReader<File>>>
where
    P: AsRef<Path>,
{
    let file = File::open(filename)?;
    use std::io::BufRead;
    Ok(io::BufReader::new(file).lines())
}
