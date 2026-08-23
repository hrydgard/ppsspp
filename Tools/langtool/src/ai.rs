use clap::ValueEnum;
use std::error::Error;

use crate::chatgpt::ChatGPT;
use crate::claude::Claude;

// gpt-5, gpt-5-mini, gpt-5-nano, gpt-4.1, gpt-4.1-mini, gpt-4.1-nano, o3, o4-mini, gpt-4o
const DEFAULT_OPENAI_MODEL: &str = "gpt-4o-mini";
// claude-opus-5, claude-sonnet-5, claude-haiku-4-5
const DEFAULT_CLAUDE_MODEL: &str = "claude-opus-5";

const OPENAI_KEY_VAR: &str = "OPENAI_API_KEY";
const CLAUDE_KEY_VAR: &str = "ANTHROPIC_API_KEY";

#[derive(Copy, Clone, Debug, PartialEq, Eq, ValueEnum)]
pub enum Provider {
    /// OpenAI, needs OPENAI_API_KEY in the environment.
    #[value(name = "openai")]
    OpenAI,
    /// Anthropic, needs ANTHROPIC_API_KEY in the environment.
    Claude,
}

pub enum Ai {
    OpenAI(ChatGPT),
    Claude(Claude),
}

impl Ai {
    /// Picks a provider and creates a client for it, if we have an API key for it.
    ///
    /// If no provider was specified on the command line, we guess from the model name if we
    /// have one, otherwise we just use whichever API key is available (Claude wins if both are).
    pub fn create(provider: Option<Provider>, model: Option<String>) -> Option<Ai> {
        let openai_key = std::env::var(OPENAI_KEY_VAR).ok();
        let claude_key = std::env::var(CLAUDE_KEY_VAR).ok();

        let provider = match provider {
            Some(provider) => provider,
            None => match model.as_deref() {
                Some(model) if model.starts_with("claude") => Provider::Claude,
                Some(model) if model.starts_with("gpt") || model.starts_with('o') => {
                    Provider::OpenAI
                }
                _ => {
                    if claude_key.is_some() {
                        Provider::Claude
                    } else {
                        Provider::OpenAI
                    }
                }
            },
        };

        // If we don't have a key for the chosen provider, we return None here and the AI
        // commands will report it and skip themselves.
        match provider {
            Provider::OpenAI => openai_key.map(|key| {
                let model = model.unwrap_or_else(|| DEFAULT_OPENAI_MODEL.to_string());
                Ai::OpenAI(ChatGPT::new(key, model))
            }),
            Provider::Claude => claude_key.map(|key| {
                let model = model.unwrap_or_else(|| DEFAULT_CLAUDE_MODEL.to_string());
                Ai::Claude(Claude::new(key, model))
            }),
        }
    }

    pub fn chat(&self, request: &str) -> Result<String, Box<dyn Error>> {
        match self {
            Ai::OpenAI(chatgpt) => chatgpt.chat(request),
            Ai::Claude(claude) => claude.chat(request),
        }
    }

    pub fn description(&self) -> String {
        match self {
            Ai::OpenAI(chatgpt) => format!("OpenAI ({})", chatgpt.model()),
            Ai::Claude(claude) => format!("Claude ({})", claude.model()),
        }
    }
}
