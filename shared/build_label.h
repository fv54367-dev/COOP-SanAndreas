#pragma once

// Build label: the CI stamps the commit here for each package (AGENTS.md rule 4); local builds keep this value.
// Kept out of config.h (in every precompiled header) so a new label does not recompile the whole project.
#define COOPSA_BUILD "2026-11-09.7"
