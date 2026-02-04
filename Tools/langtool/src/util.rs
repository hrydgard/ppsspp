use std::io::{self, Write};

#[allow(dead_code)]
pub fn ask_yes_no(question: &str) -> bool {
    loop {
        println!("{} (y/n): ", question);

        let mut input = String::new();
        io::stdin()
            .read_line(&mut input)
            .expect("Failed to read line");

        match input.trim().to_lowercase().as_str() {
            "y" | "yes" => return true,
            "n" | "no" => return false,
            _ => println!("Please enter 'y' or 'n'"),
        }
    }
}

pub fn ask_letter(question: &str, allowed_chars: &str) -> char {
    loop {
        println!("{question} ({allowed_chars}): ");
        let _ = io::stdout().flush();

        let mut input = String::new();
        io::stdin()
            .read_line(&mut input)
            .expect("Failed to read line");

        match input
            .trim()
            .to_lowercase()
            .as_str()
            .chars()
            .next()
        {
            Some(c) => {
                if allowed_chars.contains(c) {
                    return c;
                }
            }
            _ => println!("Please enter a character"),
        }
    }
}
