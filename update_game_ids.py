import json
import re

# Comprehensive list of top 100 games with their developer/publisher and standard NTSC-U and PAL game IDs/serials
games_data = [
    {
        "game": "Grand Theft Auto: San Andreas",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_209.46", "SLES_525.41", "SLPM_661.19"]
    },
    {
        "game": "Grand Theft Auto: Vice City",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_205.52", "SLES_510.61"]
    },
    {
        "game": "Grand Theft Auto III",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_200.62", "SLES_503.30"]
    },
    {
        "game": "Gran Turismo 3: A-Spec",
        "company": "Polyphony Digital",
        "game_ids": ["SCUS_971.02", "SCES_502.94"]
    },
    {
        "game": "Gran Turismo 4",
        "company": "Polyphony Digital",
        "game_ids": ["SCUS_973.28", "SCES_517.19"]
    },
    {
        "game": "God of War",
        "company": "Santa Monica Studio",
        "game_ids": ["SCUS_971.11", "SCES_530.81"]
    },
    {
        "game": "God of War II",
        "company": "Santa Monica Studio",
        "game_ids": ["SCUS_974.81", "SCES_542.06"]
    },
    {
        "game": "Metal Gear Solid 3: Snake Eater",
        "company": "Konami",
        "game_ids": ["SLUS_209.15", "SLES_525.84", "SLUS_212.43", "SLES_541.35"]
    },
    {
        "game": "Metal Gear Solid 2: Sons of Liberty",
        "company": "Konami",
        "game_ids": ["SLUS_201.44", "SLES_503.83"]
    },
    {
        "game": "Final Fantasy X",
        "company": "Square Enix",
        "game_ids": ["SLUS_203.12", "SLES_504.90"]
    },
    {
        "game": "Final Fantasy XII",
        "company": "Square Enix",
        "game_ids": ["SLUS_209.63", "SLES_543.54"]
    },
    {
        "game": "Resident Evil 4",
        "company": "Capcom",
        "game_ids": ["SLUS_211.34", "SLES_537.02"]
    },
    {
        "game": "Silent Hill 2",
        "company": "Konami",
        "game_ids": ["SLUS_202.28", "SLES_503.82", "SLES_511.56"]
    },
    {
        "game": "Kingdom Hearts",
        "company": "Square Enix",
        "game_ids": ["SLUS_203.74", "SLES_512.28"]
    },
    {
        "game": "Kingdom Hearts II",
        "company": "Square Enix",
        "game_ids": ["SLUS_210.05", "SLES_541.14"]
    },
    {
        "game": "Dragon Quest VIII: Journey of the Cursed King",
        "company": "Level-5",
        "game_ids": ["SLUS_212.07", "SLES_539.74"]
    },
    {
        "game": "Okami",
        "company": "Capcom",
        "game_ids": ["SLUS_213.61", "SLES_544.39"]
    },
    {
        "game": "Devil May Cry",
        "company": "Capcom",
        "game_ids": ["SLUS_200.22", "SLES_503.86"]
    },
    {
        "game": "Devil May Cry 3: Dante's Awakening",
        "company": "Capcom",
        "game_ids": ["SLUS_211.32", "SLES_530.38", "SLUS_213.76", "SLES_541.86"]
    },
    {
        "game": "Shadow of the Colossus",
        "company": "Team Ico",
        "game_ids": ["SCUS_974.72", "SCES_533.26"]
    },
    {
        "game": "Jak and Daxter: The Precursor Legacy",
        "company": "Naughty Dog",
        "game_ids": ["SCUS_971.24", "SCES_503.61"]
    },
    {
        "game": "Jak II",
        "company": "Naughty Dog",
        "game_ids": ["SCUS_972.65", "SCES_516.08"]
    },
    {
        "game": "Jak 3",
        "company": "Naughty Dog",
        "game_ids": ["SCUS_973.30", "SCES_524.60"]
    },
    {
        "game": "Ratchet & Clank",
        "company": "Insomniac Games",
        "game_ids": ["SCUS_971.99", "SCES_509.16"]
    },
    {
        "game": "Ratchet & Clank: Going Commando",
        "company": "Insomniac Games",
        "game_ids": ["SCUS_972.68", "SCES_516.07"]
    },
    {
        "game": "Ratchet & Clank: Up Your Arsenal",
        "company": "Insomniac Games",
        "game_ids": ["SCUS_973.53", "SCES_524.56"]
    },
    {
        "game": "Sly Cooper and the Thievius Raccoonus",
        "company": "Sucker Punch",
        "game_ids": ["SCUS_971.55", "SCES_511.90"]
    },
    {
        "game": "Sly 2: Band of Thieves",
        "company": "Sucker Punch",
        "game_ids": ["SCUS_973.16", "SCES_525.29"]
    },
    {
        "game": "Sly 3: Honor Among Thieves",
        "company": "Sucker Punch",
        "game_ids": ["SCUS_974.64", "SCES_538.45"]
    },
    {
        "game": "Burnout 3: Takedown",
        "company": "Criterion Games",
        "game_ids": ["SLUS_210.50", "SLES_525.85"]
    },
    {
        "game": "Burnout Revenge",
        "company": "Criterion Games",
        "game_ids": ["SLUS_212.42", "SLES_535.06"]
    },
    {
        "game": "Need for Speed: Underground",
        "company": "EA Games",
        "game_ids": ["SLUS_208.11", "SLES_519.67"]
    },
    {
        "game": "Need for Speed: Underground 2",
        "company": "EA Games",
        "game_ids": ["SLUS_210.65", "SLES_527.25"]
    },
    {
        "game": "Need for Speed: Most Wanted",
        "company": "EA Games",
        "game_ids": ["SLUS_212.44", "SLES_535.07"]
    },
    {
        "game": "Bully",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_212.69", "SLES_542.27"]
    },
    {
        "game": "Prince of Persia: The Sands of Time",
        "company": "Ubisoft",
        "game_ids": ["SLUS_207.43", "SLES_519.18"]
    },
    {
        "game": "Prince of Persia: Warrior Within",
        "company": "Ubisoft",
        "game_ids": ["SLUS_210.22", "SLES_528.22"]
    },
    {
        "game": "Prince of Persia: The Two Thrones",
        "company": "Ubisoft",
        "game_ids": ["SLUS_212.87", "SLES_537.77"]
    },
    {
        "game": "Tekken 5",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_210.22", "SLES_532.01"]
    },
    {
        "game": "Tekken Tag Tournament",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_200.15", "SLES_500.01"]
    },
    {
        "game": "SoulCalibur II",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_206.43", "SLES_517.02"]
    },
    {
        "game": "SoulCalibur III",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_212.23", "SLES_533.12"]
    },
    {
        "game": "Def Jam: Fight for NY",
        "company": "EA Games",
        "game_ids": ["SLUS_210.04", "SLES_525.45"]
    },
    {
        "game": "Def Jam Vendetta",
        "company": "EA Games",
        "game_ids": ["SLUS_205.65", "SLES_514.59"]
    },
    {
        "game": "Midnight Club II",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_203.22", "SLES_513.56"]
    },
    {
        "game": "Midnight Club 3: DUB Edition",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_210.29", "SLES_530.36", "SLUS_213.55", "SLES_538.25"]
    },
    {
        "game": "Tony Hawk's Pro Skater 3",
        "company": "Activision",
        "game_ids": ["SLUS_200.41", "SLES_504.38"]
    },
    {
        "game": "Tony Hawk's Pro Skater 4",
        "company": "Activision",
        "game_ids": ["SLUS_205.04", "SLES_511.96"]
    },
    {
        "game": "Tony Hawk's Underground",
        "company": "Activision",
        "game_ids": ["SLUS_207.29", "SLES_518.82"]
    },
    {
        "game": "Tony Hawk's Underground 2",
        "company": "Activision",
        "game_ids": ["SLUS_210.20", "SLES_526.47"]
    },
    {
        "game": "Black",
        "company": "Criterion Games",
        "game_ids": ["SLUS_213.76", "SLES_538.86"]
    },
    {
        "game": "Silent Hill 3",
        "company": "Konami",
        "game_ids": ["SLUS_206.33", "SLES_514.34"]
    },
    {
        "game": "Silent Hill 4: The Room",
        "company": "Konami",
        "game_ids": ["SLUS_208.73", "SLES_524.45"]
    },
    {
        "game": "Resident Evil Code: Veronica X",
        "company": "Capcom",
        "game_ids": ["SLUS_201.84", "SLES_503.06"]
    },
    {
        "game": "Resident Evil Outbreak",
        "company": "Capcom",
        "game_ids": ["SLUS_207.65", "SLES_515.86"]
    },
    {
        "game": "Mortal Kombat: Deception",
        "company": "Midway Games",
        "game_ids": ["SLUS_208.81", "SLES_527.24"]
    },
    {
        "game": "Mortal Kombat: Deadly Alliance",
        "company": "Midway Games",
        "game_ids": ["SLUS_204.23", "SLES_512.44"]
    },
    {
        "game": "Mortal Kombat: Shaolin Monks",
        "company": "Midway Games",
        "game_ids": ["SLUS_210.87", "SLES_535.24"]
    },
    {
        "game": "Mortal Kombat: Armageddon",
        "company": "Midway Games",
        "game_ids": ["SLUS_214.10", "SLES_543.16"]
    },
    {
        "game": "Spider-Man 2",
        "company": "Activision",
        "game_ids": ["SLUS_207.24", "SLES_523.72"]
    },
    {
        "game": "Star Wars: Battlefront II",
        "company": "LucasArts",
        "game_ids": ["SLUS_212.40", "SLES_535.31"]
    },
    {
        "game": "Star Wars: Battlefront",
        "company": "LucasArts",
        "game_ids": ["SLUS_208.98", "SLES_524.50"]
    },
    {
        "game": "Splinter Cell: Chaos Theory",
        "company": "Ubisoft",
        "game_ids": ["SLUS_211.06", "SLES_531.06"]
    },
    {
        "game": "Splinter Cell",
        "company": "Ubisoft",
        "game_ids": ["SLUS_203.21", "SLES_512.56"]
    },
    {
        "game": "Hitman: Blood Money",
        "company": "IO Interactive",
        "game_ids": ["SLUS_211.53", "SLES_536.56"]
    },
    {
        "game": "Hitman 2: Silent Assassin",
        "company": "IO Interactive",
        "game_ids": ["SLUS_201.44", "SLES_507.03"]
    },
    {
        "game": "Hitman: Contracts",
        "company": "IO Interactive",
        "game_ids": ["SLUS_208.82", "SLES_520.14"]
    },
    {
        "game": "Max Payne",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_202.16", "SLES_502.77"]
    },
    {
        "game": "Max Payne 2: The Fall of Max Payne",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_207.28", "SLES_520.91"]
    },
    {
        "game": "Onimusha: Warlords",
        "company": "Capcom",
        "game_ids": ["SLUS_200.18", "SLES_501.81"]
    },
    {
        "game": "Onimusha 2: Samurai's Destiny",
        "company": "Capcom",
        "game_ids": ["SLUS_203.93", "SLES_509.30"]
    },
    {
        "game": "Onimusha 3: Demon Siege",
        "company": "Capcom",
        "game_ids": ["SLUS_206.94", "SLES_521.57"]
    },
    {
        "game": "Beyond Good & Evil",
        "company": "Ubisoft",
        "game_ids": ["SLUS_202.04", "SLES_519.16"]
    },
    {
        "game": "Katamari Damacy",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_210.08"]
    },
    {
        "game": "We Love Katamari",
        "company": "Bandai Namco",
        "game_ids": ["SLUS_212.37", "SLES_540.35"]
    },
    {
        "game": "SSX Tricky",
        "company": "EA Sports",
        "game_ids": ["SLUS_203.26", "SLES_505.77"]
    },
    {
        "game": "SSX 3",
        "company": "EA Sports",
        "game_ids": ["SLUS_207.72", "SLES_516.48"]
    },
    {
        "game": "NBA Street Vol. 2",
        "company": "EA Sports",
        "game_ids": ["SLUS_206.50", "SLES_515.68"]
    },
    {
        "game": "NBA Street V3",
        "company": "EA Sports",
        "game_ids": ["SLUS_209.68", "SLES_529.56"]
    },
    {
        "game": "Destroy All Humans!",
        "company": "THQ",
        "game_ids": ["SLUS_209.79", "SLES_531.60"]
    },
    {
        "game": "Destroy All Humans! 2",
        "company": "THQ",
        "game_ids": ["SLUS_214.37", "SLES_542.45"]
    },
    {
        "game": "The Simpsons Hit & Run",
        "company": "Vivendi Games",
        "game_ids": ["SLUS_206.24", "SLES_518.23"]
    },
    {
        "game": "The Simpsons Road Rage",
        "company": "Vivendi Games",
        "game_ids": ["SLUS_201.99", "SLES_504.60"]
    },
    {
        "game": "Crash Bandicoot: The Wrath of Cortex",
        "company": "Traveller's Tales",
        "game_ids": ["SLUS_202.38", "SLES_503.86"]
    },
    {
        "game": "Crash Twinsanity",
        "company": "Traveller's Tales",
        "game_ids": ["SLUS_209.09", "SLES_525.68"]
    },
    {
        "game": "Spyro: Enter the Dragonfly",
        "company": "Universal Interactive",
        "game_ids": ["SLUS_204.53", "SLES_511.53"]
    },
    {
        "game": "Guitar Hero",
        "company": "RedOctane",
        "game_ids": ["SLUS_212.24"]
    },
    {
        "game": "Guitar Hero II",
        "company": "RedOctane",
        "game_ids": ["SLUS_214.43", "SLES_544.35"]
    },
    {
        "game": "Guitar Hero III: Legends of Rock",
        "company": "Activision",
        "game_ids": ["SLUS_216.71", "SLES_549.44"]
    },
    {
        "game": "Shin Megami Tensei: Persona 3",
        "company": "Atlus",
        "game_ids": ["SLUS_215.69", "SLES_550.18"]
    },
    {
        "game": "Shin Megami Tensei: Persona 4",
        "company": "Atlus",
        "game_ids": ["SLUS_217.82", "SLES_554.73"]
    },
    {
        "game": "Ape Escape 2",
        "company": "Sony Computer Ent.",
        "game_ids": ["SLUS_206.85", "SCES_509.64"]
    },
    {
        "game": "Ape Escape 3",
        "company": "Sony Computer Ent.",
        "game_ids": ["SLUS_211.77", "SCES_536.42"]
    },
    {
        "game": "Manhunt",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_208.27", "SLES_520.23"]
    },
    {
        "game": "Manhunt 2",
        "company": "Rockstar Games",
        "game_ids": ["SLUS_216.13", "SLES_548.19"]
    },
    {
        "game": "Champions of Norrath",
        "company": "Sony Online Ent.",
        "game_ids": ["SLUS_205.65", "SLES_523.25"]
    },
    {
        "game": "Dark Cloud",
        "company": "Level-5",
        "game_ids": ["SCUS_971.11", "SCES_502.52"]
    },
    {
        "game": "Dark Cloud 2",
        "company": "Level-5",
        "game_ids": ["SCUS_972.13", "SCES_516.24"]
    },
    {
        "game": "Xenosaga Episode I: Der Wille zur Macht",
        "company": "Monolith Soft",
        "game_ids": ["SLUS_204.69"]
    },
    {
        "game": "Disgaea: Hour of Darkness",
        "company": "Nippon Ichi",
        "game_ids": ["SLUS_206.66", "SLES_523.29"]
    }
]

# Write games_list.json
json_path = "/home/irfan/OPL-Fresh/games_list.json"
with open(json_path, 'w', encoding='utf-8') as f:
    json.dump(games_data, f, indent=4)
print(f"Successfully updated {json_path}")

# Generate optimized C struct code
c_code_lines = [
    "typedef struct {",
    "    const char *serial;",
    "    const char *cleanTitle;",
    "    const char *company;",
    "} popular_game_t;\n",
    "static const popular_game_t gPopularGames[] = {"
]

def clean_alphanumeric(text):
    return re.sub(r'[^a-zA-Z0-9]', '', text).lower()

for item in games_data:
    clean_title = clean_alphanumeric(item["game"])
    for serial in item["game_ids"]:
        clean_serial = clean_alphanumeric(serial)
        c_code_lines.append(f'    {{"{clean_serial}", "{clean_title}", "{item["company"]}"}},')

# Remove trailing comma on last element and close array
c_code_lines[-1] = c_code_lines[-1][:-1]
c_code_lines.append("};")

c_code = "\n".join(c_code_lines)
c_output_path = "/home/irfan/OPL-Fresh/c_struct_v2.txt"
with open(c_output_path, 'w', encoding='utf-8') as f:
    f.write(c_code)
print(f"Successfully generated C struct code to {c_output_path}")
