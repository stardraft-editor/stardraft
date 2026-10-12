// eudplib's names of the game's data entries (eudplib/core/rawtrigger/strdict: DefUnitDict, DefWeaponDict, ...; the first
// name of each id), so the generated epScript can write a name, not a number: $U("Zerg Zergling"),
// EncodeWeapon("Gauss Rifle"). Made from euddraft 0.11.0.1. Do not edit.

static const char* const kEudUnitNames[] = {
    "Terran Marine",  // 0
    "Terran Ghost",  // 1
    "Terran Vulture",  // 2
    "Terran Goliath",  // 3
    "Goliath Turret",  // 4
    "Terran Siege Tank (Tank Mode)",  // 5
    "Siege Tank Turret (Tank Mode)",  // 6
    "Terran SCV",  // 7
    "Terran Wraith",  // 8
    "Terran Science Vessel",  // 9
    "Gui Montag",  // 10
    "Terran Dropship",  // 11
    "Terran Battlecruiser",  // 12
    "Spider Mine",  // 13
    "Nuclear Missile",  // 14
    "Terran Civilian",  // 15
    "Sarah Kerrigan",  // 16
    "Alan Schezar",  // 17
    "Alan Schezar Turret",  // 18
    "Jim Raynor (Vulture)",  // 19
    "Jim Raynor (Marine)",  // 20
    "Tom Kazansky",  // 21
    "Magellan",  // 22
    "Edmund Duke (Tank Mode)",  // 23
    "Edmund Duke Turret (Tank Mode)",  // 24
    "Edmund Duke (Siege Mode)",  // 25
    "Edmund Duke Turret (Siege Mode)",  // 26
    "Arcturus Mengsk",  // 27
    "Hyperion",  // 28
    "Norad II (Battlecruiser)",  // 29
    "Terran Siege Tank (Siege Mode)",  // 30
    "Siege Tank Turret (Siege Mode)",  // 31
    "Terran Firebat",  // 32
    "Scanner Sweep",  // 33
    "Terran Medic",  // 34
    "Zerg Larva",  // 35
    "Zerg Egg",  // 36
    "Zerg Zergling",  // 37
    "Zerg Hydralisk",  // 38
    "Zerg Ultralisk",  // 39
    "Zerg Broodling",  // 40
    "Zerg Drone",  // 41
    "Zerg Overlord",  // 42
    "Zerg Mutalisk",  // 43
    "Zerg Guardian",  // 44
    "Zerg Queen",  // 45
    "Zerg Defiler",  // 46
    "Zerg Scourge",  // 47
    "Torrasque",  // 48
    "Matriarch",  // 49
    "Infested Terran",  // 50
    "Infested Kerrigan",  // 51
    "Unclean One",  // 52
    "Hunter Killer",  // 53
    "Devouring One",  // 54
    "Kukulza (Mutalisk)",  // 55
    "Kukulza (Guardian)",  // 56
    "Yggdrasill",  // 57
    "Terran Valkyrie",  // 58
    "Mutalisk Cocoon",  // 59
    "Protoss Corsair",  // 60
    "Protoss Dark Templar",  // 61
    "Zerg Devourer",  // 62
    "Protoss Dark Archon",  // 63
    "Protoss Probe",  // 64
    "Protoss Zealot",  // 65
    "Protoss Dragoon",  // 66
    "Protoss High Templar",  // 67
    "Protoss Archon",  // 68
    "Protoss Shuttle",  // 69
    "Protoss Scout",  // 70
    "Protoss Arbiter",  // 71
    "Protoss Carrier",  // 72
    "Protoss Interceptor",  // 73
    "Dark Templar",  // 74
    "Zeratul",  // 75
    "Tassadar/Zeratul",  // 76
    "Fenix (Zealot)",  // 77
    "Fenix (Dragoon)",  // 78
    "Tassadar",  // 79
    "Mojo",  // 80
    "Warbringer",  // 81
    "Gantrithor",  // 82
    "Protoss Reaver",  // 83
    "Protoss Observer",  // 84
    "Protoss Scarab",  // 85
    "Danimoth",  // 86
    "Aldaris",  // 87
    "Artanis",  // 88
    "Rhynadon",  // 89
    "Bengalaas",  // 90
    "Cargo Ship",  // 91
    nullptr,  // 92
    "Scantid",  // 93
    "Kakaru",  // 94
    "Ragnasaur",  // 95
    "Ursadon",  // 96
    "Lurker Egg",  // 97
    "Raszagal",  // 98
    "Samir Duran",  // 99
    "Alexei Stukov",  // 100
    "Map Revealer",  // 101
    "Gerard DuGalle",  // 102
    "Zerg Lurker",  // 103
    "Infested Duran",  // 104
    "Disruption Web",  // 105
    "Terran Command Center",  // 106
    "Terran Comsat Station",  // 107
    "Terran Nuclear Silo",  // 108
    "Terran Supply Depot",  // 109
    "Terran Refinery",  // 110
    "Terran Barracks",  // 111
    "Terran Academy",  // 112
    "Terran Factory",  // 113
    "Terran Starport",  // 114
    "Terran Control Tower",  // 115
    "Terran Science Facility",  // 116
    "Terran Covert Ops",  // 117
    "Terran Physics Lab",  // 118
    "Starbase",  // 119
    "Terran Machine Shop",  // 120
    "Repair Bay",  // 121
    "Terran Engineering Bay",  // 122
    "Terran Armory",  // 123
    "Terran Missile Turret",  // 124
    "Terran Bunker",  // 125
    "Norad II (Crashed)",  // 126
    "Ion Cannon",  // 127
    "Uraj Crystal",  // 128
    "Khalis Crystal",  // 129
    "Infested Command Center",  // 130
    "Zerg Hatchery",  // 131
    "Zerg Lair",  // 132
    "Zerg Hive",  // 133
    "Zerg Nydus Canal",  // 134
    "Zerg Hydralisk Den",  // 135
    "Zerg Defiler Mound",  // 136
    "Zerg Greater Spire",  // 137
    "Zerg Queen's Nest",  // 138
    "Zerg Evolution Chamber",  // 139
    "Zerg Ultralisk Cavern",  // 140
    "Zerg Spire",  // 141
    "Zerg Spawning Pool",  // 142
    "Zerg Creep Colony",  // 143
    "Zerg Spore Colony",  // 144
    nullptr,  // 145
    "Zerg Sunken Colony",  // 146
    "Zerg Overmind (With Shell)",  // 147
    "Zerg Overmind",  // 148
    "Zerg Extractor",  // 149
    "Mature Chrysalis",  // 150
    "Zerg Cerebrate",  // 151
    "Zerg Cerebrate Daggoth",  // 152
    nullptr,  // 153
    "Protoss Nexus",  // 154
    "Protoss Robotics Facility",  // 155
    "Protoss Pylon",  // 156
    "Protoss Assimilator",  // 157
    "Protoss Unused type   1",  // 158
    "Protoss Observatory",  // 159
    "Protoss Gateway",  // 160
    "Protoss Unused type   2",  // 161
    "Protoss Photon Cannon",  // 162
    "Protoss Citadel of Adun",  // 163
    "Protoss Cybernetics Core",  // 164
    "Protoss Templar Archives",  // 165
    "Protoss Forge",  // 166
    "Protoss Stargate",  // 167
    "Stasis Cell/Prison",  // 168
    "Protoss Fleet Beacon",  // 169
    "Protoss Arbiter Tribunal",  // 170
    "Protoss Robotics Support Bay",  // 171
    "Protoss Shield Battery",  // 172
    "Khaydarin Crystal Formation",  // 173
    "Protoss Temple",  // 174
    "Xel'Naga Temple",  // 175
    "Mineral Field (Type 1)",  // 176
    "Mineral Field (Type 2)",  // 177
    "Mineral Field (Type 3)",  // 178
    "Cave",  // 179
    "Cave-in",  // 180
    "Cantina",  // 181
    "Mining Platform",  // 182
    "Independent Command Center",  // 183
    "Independent Starport",  // 184
    "Jump Gate",  // 185
    "Ruins",  // 186
    "Kyadarin Crystal Formation",  // 187
    "Vespene Geyser",  // 188
    "Warp Gate",  // 189
    "Psi Disrupter",  // 190
    "Zerg Marker",  // 191
    "Terran Marker",  // 192
    "Protoss Marker",  // 193
    "Zerg Beacon",  // 194
    "Terran Beacon",  // 195
    "Protoss Beacon",  // 196
    "Zerg Flag Beacon",  // 197
    "Terran Flag Beacon",  // 198
    "Protoss Flag Beacon",  // 199
    "Power Generator",  // 200
    "Overmind Cocoon",  // 201
    "Dark Swarm",  // 202
    "Floor Missile Trap",  // 203
    "Floor Hatch (Unused)",  // 204
    "Left Upper Level Door",  // 205
    "Right Upper Level Door",  // 206
    "Left Pit Door",  // 207
    "Right Pit Door",  // 208
    "Floor Gun Trap",  // 209
    "Left Wall Missile Trap",  // 210
    "Left Wall Flame Trap",  // 211
    "Right Wall Missile Trap",  // 212
    "Right Wall Flame Trap",  // 213
    "Start Location",  // 214
    "Flag",  // 215
    "Young Chrysalis",  // 216
    "Psi Emitter",  // 217
    "Data Disc",  // 218
    "Khaydarin Crystal",  // 219
    "Mineral Chunk (Type 1)",  // 220
    "Mineral Chunk (Type 2)",  // 221
    "Vespene Orb (Protoss Type 1)",  // 222
    "Vespene Orb (Protoss Type 2)",  // 223
    "Vespene Sac (Zerg Type 1)",  // 224
    "Vespene Sac (Zerg Type 2)",  // 225
    "Vespene Tank (Terran Type 1)",  // 226
    "Vespene Tank (Terran Type 2)",  // 227
    nullptr,  // 228
    "Any unit",  // 229
    "Men",  // 230
    "Buildings",  // 231
    "Factories",  // 232
};
static const char* const kEudWeaponNames[] = {
    "Gauss Rifle",  // 0
    "Gauss Rifle (Raynor)",  // 1
    "C-10 Concussion Rifle",  // 2
    "C-10 Concussion Rifle (Kerrigan)",  // 3
    "Fragmentation Grenade",  // 4
    "Fragmentation Grenade (Raynor)",  // 5
    "Spider Mines",  // 6
    "Twin Autocannons",  // 7
    "Hellfire Missile Pack",  // 8
    "Twin Autocannons (Schezar)",  // 9
    "Hellfire Missile Pack (Schezar)",  // 10
    "Arclite Cannon",  // 11
    "Arclite Cannon (Duke)",  // 12
    "Fusion Cutter",  // 13
    "Fusion Cutter (Harvest)",  // 14
    "Gemini Missiles",  // 15
    "Burst Lasers",  // 16
    "Gemini Missiles (Kazansky)",  // 17
    "Burst Lasers (Kazansky)",  // 18
    "ATS Laser Battery",  // 19
    "ATA Laser Battery",  // 20
    "ATS Laser Battery (Norad II+Mengsk+DuGalle)",  // 21
    "ATA Laser Battery (Norad II+Mengsk+DuGalle)",  // 22
    "ATS Laser Battery (Hyperion)",  // 23
    "ATA Laser Battery (Hyperion)",  // 24
    "Flame Thrower",  // 25
    "Flame Thrower (Montag)",  // 26
    "Arclite Shock Cannon",  // 27
    "Arclite Shock Cannon (Duke)",  // 28
    "Longbolt Missiles",  // 29
    "Yamato Gun",  // 30
    "Nuclear Missile",  // 31
    "Lockdown",  // 32
    "EMP Shockwave",  // 33
    "Irradiate",  // 34
    "Claws",  // 35
    "Claws (Devouring One)",  // 36
    "Claws (Infested Kerrigan)",  // 37
    "Needle Spines",  // 38
    "Needle Spines (Hunter Killer)",  // 39
    "Kaiser Blades",  // 40
    "Kaiser Blades (Torrasque)",  // 41
    "Toxic Spores",  // 42
    "Spines",  // 43
    "Spines (Harvest)",  // 44
    "Acid Spray",  // 45
    "Acid Spore",  // 46
    "Acid Spore (Kukulza)",  // 47
    "Glave Wurm",  // 48
    "Glave Wurm (Kukulza)",  // 49
    "Venom",  // 50
    "Venom (Unclean One)",  // 51
    "Seeker Spores",  // 52
    "Subterranean Tentacle",  // 53
    "Suicide (Infested Terran)",  // 54
    "Suicide (Scourge)",  // 55
    "Parasite",  // 56
    "Spawn Broodlings",  // 57
    "Ensnare",  // 58
    "Dark Swarm",  // 59
    "Plague",  // 60
    "Consume",  // 61
    "Particle Beam",  // 62
    "Particle Beam (Harvest)",  // 63
    "Psi Blades",  // 64
    "Psi Blades (Fenix)",  // 65
    "Phase Disruptor",  // 66
    "Phase Disruptor (Fenix)",  // 67
    "Psi Assault",  // 68
    "Psi Assault (Tassadar)",  // 69
    "Psionic Shockwave",  // 70
    "Psionic Shockwave (Tassadar/Zeratul)",  // 71
    nullptr,  // 72
    "Dual Photon Blasters",  // 73
    "Anti-matter Missiles",  // 74
    "Dual Photon Blasters (Mojo)",  // 75
    "Anti-matter Missiles (Mojo)",  // 76
    "Phase Disruptor Cannon",  // 77
    "Phase Disruptor Cannon (Danimoth)",  // 78
    "Pulse Cannon",  // 79
    "STS Photon Cannon",  // 80
    "STA Photon Cannon",  // 81
    "Scarab",  // 82
    "Stasis Field",  // 83
    "Psi Storm",  // 84
    "Warp Blades (Zeratul)",  // 85
    "Warp Blades (Hero)",  // 86
    "Missiles",  // 87
    "Laser Battery1",  // 88
    "Tormentor Missiles",  // 89
    "Bombs",  // 90
    "Raider Gun",  // 91
    "Laser Battery2",  // 92
    "Laser Battery3",  // 93
    "Dual Photon Blasters (Unused)",  // 94
    "Flechette Grenade",  // 95
    "Twin Autocannons (Floor Trap)",  // 96
    "Hellfire Missile Pack (Wall Trap)",  // 97
    "Flame Thrower (Wall Trap)",  // 98
    "Hellfire Missile Pack (Floor Trap)",  // 99
    "Neutron Flare",  // 100
    "Disruption Web",  // 101
    "Restoration",  // 102
    "Halo Rockets",  // 103
    "Corrosive Acid",  // 104
    "Mind Control",  // 105
    "Feedback",  // 106
    "Optical Flare",  // 107
    "Maelstrom",  // 108
    "Subterranean Spines",  // 109
    "Gauss Rifle0",  // 110
    "Warp Blades",  // 111
    "C-10 Concussion Rifle (Samir Duran)",  // 112
    "C-10 Concussion Rifle (Infested Duran)",  // 113
    "Dual Photon Blasters (Artanis)",  // 114
    "Anti-matter Missiles (Artanis)",  // 115
    "C-10 Concussion Rifle (Stukov)",  // 116
    "Gauss Rifle1",  // 117
    "Gauss Rifle2",  // 118
    "Gauss Rifle3",  // 119
    "Gauss Rifle4",  // 120
    "Gauss Rifle5",  // 121
    "Gauss Rifle6",  // 122
    "Gauss Rifle7",  // 123
    "Gauss Rifle8",  // 124
    "Gauss Rifle9",  // 125
    "Gauss Rifle10",  // 126
    "Gauss Rifle11",  // 127
    "Gauss Rifle12",  // 128
    "Gauss Rifle13",  // 129
};
static const char* const kEudFlingyNames[] = {
    "Scourge",  // 0
    "Broodling",  // 1
    "Infested Terran",  // 2
    "Guardian Cocoon",  // 3
    "Defiler",  // 4
    "Drone",  // 5
    "Egg",  // 6
    "Guardian",  // 7
    "Hydralisk",  // 8
    "Infested Kerrigan",  // 9
    "Larva",  // 10
    "Mutalisk",  // 11
    "Overlord",  // 12
    "Queen",  // 13
    "Ultralisk",  // 14
    "Zergling",  // 15
    "Cerebrate",  // 16
    "Infested Command Center",  // 17
    "Spawning Pool",  // 18
    "Mature Chrysalis",  // 19
    "Evolution Chamber",  // 20
    "Creep Colony",  // 21
    "Hatchery",  // 22
    "Hive",  // 23
    "Lair",  // 24
    "Sunken Colony",  // 25
    "Greater Spire",  // 26
    "Defiler Mound",  // 27
    "Queen's Nest",  // 28
    "Nydus Canal",  // 29
    "Overmind With Shell",  // 30
    "Overmind Without Shell",  // 31
    "Ultralisk Cavern",  // 32
    "Extractor",  // 33
    "Hydralisk Den",  // 34
    "Spire",  // 35
    "Spore Colony",  // 36
    "Arbiter",  // 37
    "Archon Energy",  // 38
    "Carrier",  // 39
    "Dragoon",  // 40
    "Interceptor",  // 41
    "Probe",  // 42
    "Scout",  // 43
    "Shuttle",  // 44
    "High Templar",  // 45
    "Dark Templar (Hero)",  // 46
    "Reaver",  // 47
    "Scarab",  // 48
    "Zealot",  // 49
    "Observer",  // 50
    "Templar Archives",  // 51
    "Assimilator",  // 52
    "Observatory",  // 53
    "Citadel of Adun",  // 54
    "Forge",  // 55
    "Gateway",  // 56
    "Cybernetics Core",  // 57
    "Khaydarin Crystal Formation",  // 58
    "Nexus",  // 59
    "Photon Cannon",  // 60
    "Arbiter Tribunal",  // 61
    "Pylon",  // 62
    "Robotics Facility",  // 63
    "Shield Battery",  // 64
    "Stargate",  // 65
    "Stasis Cell/Prison",  // 66
    "Robotics Support Bay",  // 67
    "Protoss Temple",  // 68
    "Fleet Beacon",  // 69
    "Battlecruiser",  // 70
    "Civilian",  // 71
    "Dropship",  // 72
    "Firebat",  // 73
    "Ghost",  // 74
    "Goliath Base",  // 75
    "Goliath Turret",  // 76
    "Sarah Kerrigan",  // 77
    "Marine",  // 78
    "Scanner Sweep",  // 79
    "Wraith",  // 80
    "SCV",  // 81
    "Siege Tank (Tank) Base",  // 82
    "Siege Tank (Tank) Turret",  // 83
    "Siege Tank (Siege) Base",  // 84
    "Siege Tank (Siege) Turret",  // 85
    "Science Vessel (Base)",  // 86
    "Science Vessel (Turret)",  // 87
    "Vulture",  // 88
    "Spider Mine",  // 89
    "Academy",  // 90
    "Barracks",  // 91
    "Armory",  // 92
    "Comsat Station",  // 93
    "Command Center",  // 94
    "Supply Depot",  // 95
    "Control Tower",  // 96
    "Factory",  // 97
    "Covert Ops",  // 98
    "Ion Cannon",  // 99
    "Machine Shop",  // 100
    "Missile Turret",  // 101
    "Crashed Battlecruiser",  // 102
    "Physics Lab",  // 103
    "Bunker",  // 104
    "Refinery",  // 105
    "Immobile Barracks",  // 106
    "Science Facility",  // 107
    "Nuke Silo",  // 108
    "Nuclear Missile",  // 109
    "Starport",  // 110
    "Engineering Bay",  // 111
    "Terran Construction (Large)",  // 112
    "Terran Construction (Small)",  // 113
    "Ragnasaur",  // 114
    "Rhynadon",  // 115
    "Bengalaas",  // 116
    "Vespene Geyser",  // 117
    "Mineral Field Type1",  // 118
    "Mineral Field Type2",  // 119
    "Mineral Field Type3",  // 120
    "Independent Starport",  // 121
    "Zerg Beacon",  // 122
    "Terran Beacon",  // 123
    "Protoss Beacon",  // 124
    "Dark Swarm",  // 125
    "Flag",  // 126
    "Young Chrysalis",  // 127
    "Psi Emitter",  // 128
    "Data Disc",  // 129
    "Khaydarin Crystal",  // 130
    "Mineral Chunk Type1",  // 131
    "Mineral Chunk Type2",  // 132
    "Protoss Gas Orb Type1",  // 133
    "Protoss Gas Orb Type2",  // 134
    "Zerg Gas Sac Type1",  // 135
    "Zerg Gas Sac Type2",  // 136
    "Terran Gas Tank Type1",  // 137
    "Terran Gas Tank Type2",  // 138
    "Map Revealer",  // 139
    "Start Location",  // 140
    "Fusion Cutter Hit",  // 141
    "Gauss Rifle Hit",  // 142
    "C-10 Canister Rifle Hit",  // 143
    "Gemini Missiles",  // 144
    "Fragmentation Grenade",  // 145
    "Lockdown/LongBolt/Hellfire Missile",  // 146
    nullptr,  // 147
    "ATS/ATA Laser Battery",  // 148
    "Burst Lasers",  // 149
    "Arclite Shock Cannon Hit",  // 150
    "EMP Missile",  // 151
    "Dual Photon Blasters Hit",  // 152
    "Particle Beam Hit",  // 153
    "Anti-Matter Missile",  // 154
    "Pulse Cannon",  // 155
    "Psionic Shockwave Hit",  // 156
    "Psionic Storm",  // 157
    "Yamato Gun",  // 158
    "Phase Disruptor",  // 159
    "STA/STS Photon Cannon Overlay",  // 160
    "Sunken Colony Tentacle",  // 161
    "Venom",  // 162
    "Acid Spore",  // 163
    "Unknown164",  // 164
    "Glave Wurm",  // 165
    "Seeker Spores",  // 166
    "Queen Spell Carrier",  // 167
    "Plague Cloud",  // 168
    "Consume",  // 169
    "Ensnare",  // 170
    "Needle Spine Hit",  // 171
    "White Circle (Invisible)",  // 172
    "Left Upper Level Door",  // 173
    "Right Upper Level Door",  // 174
    "Substructure Left Door",  // 175
    "Substructure Right Door",  // 176
    "Substructure Opening Hole",  // 177
    "Floor Gun Trap",  // 178
    "Floor Missile Trap",  // 179
    "Wall Missile Trap",  // 180
    "Wall Missile Trap2",  // 181
    "Wall Flame Trap",  // 182
    "Wall Flame Trap2",  // 183
    "Lurker Egg",  // 184
    "Devourer",  // 185
    "Lurker",  // 186
    "Dark Archon Energy",  // 187
    "Dark Templar (Unit)",  // 188
    "Medic",  // 189
    "Valkyrie",  // 190
    "Corsair",  // 191
    "Disruption Web",  // 192
    "Overmind Cocoon",  // 193
    "Psi Disrupter",  // 194
    "Warp Gate",  // 195
    "Power Generator",  // 196
    "Xel'Naga Temple",  // 197
    "Scantid",  // 198
    "Kakaru",  // 199
    "Ursadon",  // 200
    "Optical Flare Grenade",  // 201
    "Halo Rockets",  // 202
    "Subterranean Spines",  // 203
    "Corrosive Acid Shot",  // 204
    "Corrosive Acid Hit",  // 205
    "Neutron Flare",  // 206
    "Uraj",  // 207
    "Khalis",  // 208
};
static const char* const kEudSpriteNames[] = {
    "2/38 Ash",  // 0
    "2/39 Ash",  // 1
    "2/41 Ash",  // 2
    "2/40 Ash",  // 3
    "2/42 Ash",  // 4
    "2/43 Ash",  // 5
    "2/44 Ash",  // 6
    "2/1 Ash",  // 7
    "2/4 Ash",  // 8
    "2/5 Ash",  // 9
    "2/30 Ash",  // 10
    "2/28 Ash",  // 11
    "2/29 Ash",  // 12
    "4/1 Ash",  // 13
    "4/2 Ash",  // 14
    "4/3 Ash",  // 15
    "4/56 Jungle",  // 16
    "4/57 Jungle",  // 17
    "4/58 Jungle",  // 18
    "4/59 Jungle",  // 19
    "9/5 Jungle",  // 20
    "9/6 Jungle",  // 21
    "9/7 Jungle",  // 22
    "4/51 Jungle",  // 23
    "4/52 Jungle",  // 24
    "4/54 Jungle",  // 25
    "4/53 Jungle",  // 26
    "9/1 Jungle",  // 27
    "9/2 Jungle",  // 28
    "9/3 Jungle",  // 29
    "9/4 Jungle",  // 30
    "4/12 Jungle",  // 31
    "4/13 Jungle",  // 32
    "4/1 Jungle",  // 33
    "4/3 Jungle",  // 34
    "4/2 Jungle",  // 35
    "4/5 Jungle",  // 36
    "4/4 Jungle",  // 37
    "4/9 Jungle",  // 38
    "4/10 Jungle",  // 39
    "5/5 Jungle",  // 40
    "5/7 Jungle",  // 41
    "5/6 Jungle",  // 42
    "5/9 Jungle",  // 43
    "5/8 Jungle",  // 44
    "4/6 Jungle",  // 45
    "4/7 Jungle",  // 46
    "4/17 Jungle",  // 47
    "13/4 Jungle",  // 48
    "11/5 Jungle",  // 49
    "11/6 Jungle",  // 50
    "11/7 Jungle",  // 51
    "11/8 Jungle",  // 52
    "11/10 Jungle",  // 53
    "11/11 Jungle",  // 54
    "11/12 Jungle",  // 55
    "7/4 Platform",  // 56
    "7/5 Platform",  // 57
    "7/6 Platform",  // 58
    "7/1 Platform",  // 59
    "7/2 Platform",  // 60
    "7/3 Platform",  // 61
    "7/9 Platform",  // 62
    "7/10 Platform",  // 63
    "7/8 Platform",  // 64
    "7/7 Platform",  // 65
    "7/26 Platform",  // 66
    "7/24 Platform",  // 67
    "7/28 Platform",  // 68
    "7/27 Platform",  // 69
    "7/25 Platform",  // 70
    "7/29 Platform",  // 71
    "7/30 Platform",  // 72
    "7/31 Platform",  // 73
    "12/1 Platform",  // 74
    "9/27 Platform",  // 75
    "5/54 Badlands",  // 76
    "5/55 Badlands",  // 77
    "5/56 Badlands",  // 78
    "5/57 Badlands",  // 79
    "6/16 Badlands",  // 80
    "6/17 Badlands",  // 81
    "6/20 Badlands",  // 82
    "6/21 Badlands",  // 83
    "5/10 Badlands",  // 84
    "5/50 Badlands",  // 85
    "5/52 Badlands",  // 86
    "5/53 Badlands",  // 87
    "5/51 Badlands",  // 88
    "6/3 Badlands",  // 89
    "11/3 Badlands",  // 90
    "11/8 Badlands",  // 91
    "11/6 Badlands",  // 92
    "11/7 Badlands",  // 93
    "11/9 Badlands",  // 94
    "11/10 Badlands",  // 95
    "11/11 Badlands",  // 96
    "11/12 Badlands",  // 97
    "11/13 Badlands",  // 98
    "11/14 Badlands",  // 99
    "1/13 Badlands",  // 100
    "1/9 Badlands",  // 101
    "1/11 Badlands",  // 102
    "1/14 Badlands",  // 103
    "1/10 Badlands",  // 104
    "1/12 Badlands",  // 105
    "1/15 Badlands",  // 106
    "1/7 Badlands",  // 107
    "1/5 Badlands",  // 108
    "1/16 Badlands",  // 109
    "1/8 Badlands",  // 110
    "1/6 Badlands1",  // 111
    "1/6 Badlands2",  // 112
    "1/6 Badlands3",  // 113
    "1/6 Badlands4",  // 114
    "1/6 Badlands5",  // 115
    "1/6 Badlands6",  // 116
    "1/6 Badlands7",  // 117
    "1/6 Badlands8",  // 118
    "4/15 Installation1",  // 119
    "4/15 Installation2",  // 120
    "3/9 Installation",  // 121
    "3/10 Installation",  // 122
    "3/11 Installation",  // 123
    "3/12 Installation",  // 124
    "1/6 Badlands9",  // 125
    "1/6 Badlands10",  // 126
    "3/1 Installation",  // 127
    "3/2 Installation",  // 128
    "1/6 Badlands11",  // 129
    "Scourge",  // 130
    "Scourge Death",  // 131
    "Scourge Explosion",  // 132
    "Broodling",  // 133
    "Broodling Remnants",  // 134
    "Infested Terran",  // 135
    "Infested Terran Explosion",  // 136
    "Guardian Cocoon",  // 137
    "Defiler",  // 138
    "Defiler Remnants",  // 139
    "Drone",  // 140
    "Drone Remnants",  // 141
    "Egg",  // 142
    "Egg Remnants",  // 143
    "Guardian",  // 144
    "Guardian Death",  // 145
    "Hydralisk",  // 146
    "Hydralisk Remnants",  // 147
    "Infested Kerrigan",  // 148
    "Larva",  // 149
    "Larva Remnants",  // 150
    "Mutalisk",  // 151
    "Mutalisk Death",  // 152
    "Overlord",  // 153
    "Overlord Death",  // 154
    "Queen",  // 155
    "Queen Death",  // 156
    "Ultralisk",  // 157
    "Ultralisk Remnants",  // 158
    "Zergling",  // 159
    "Zergling Remnants",  // 160
    "Cerebrate",  // 161
    "Infested Command Center",  // 162
    "Spawning Pool",  // 163
    "Mature Chrysalis",  // 164
    "Evolution Chamber",  // 165
    "Creep Colony",  // 166
    "Hatchery",  // 167
    "Hive",  // 168
    "Lair",  // 169
    "Sunken Colony",  // 170
    "Greater Spire",  // 171
    "Defiler Mound",  // 172
    "Queen's Nest",  // 173
    "Nydus Canal",  // 174
    "Overmind With Shell",  // 175
    "Overmind Without Shell",  // 176
    "Ultralisk Cavern",  // 177
    "Extractor",  // 178
    "Hydralisk Den",  // 179
    "Spire",  // 180
    "Spore Colony",  // 181
    "Zerg Building Spawn (Small)",  // 182
    "Zerg Building Spawn (Medium)",  // 183
    "Zerg Building Spawn (Large)",  // 184
    "Zerg Building Explosion",  // 185
    "Zerg Building Rubble (Small)",  // 186
    "Zerg Building Rubble (Large)",  // 187
    "Arbiter",  // 188
    "Archon Energy",  // 189
    "Carrier",  // 190
    "Dragoon",  // 191
    "Dragoon Remnants",  // 192
    "Interceptor",  // 193
    "Probe",  // 194
    "Scout",  // 195
    "Shuttle",  // 196
    "High Templar",  // 197
    "Dark Templar (Hero)",  // 198
    "Reaver",  // 199
    "Scarab",  // 200
    "Zealot",  // 201
    "Observer",  // 202
    "Templar Archives",  // 203
    "Assimilator",  // 204
    "Observatory",  // 205
    "Citadel of Adun",  // 206
    "Forge",  // 207
    "Gateway",  // 208
    "Cybernetics Core",  // 209
    "Khaydarin Crystal Formation",  // 210
    "Nexus",  // 211
    "Photon Cannon",  // 212
    "Arbiter Tribunal",  // 213
    "Pylon",  // 214
    "Robotics Facility",  // 215
    "Shield Battery",  // 216
    "Stargate",  // 217
    "Stasis Cell/Prison",  // 218
    "Robotics Support Bay",  // 219
    "Protoss Temple",  // 220
    "Fleet Beacon",  // 221
    "Explosion (Large)",  // 222
    "Protoss Building Rubble (Small)",  // 223
    "Protoss Building Rubble (Large)",  // 224
    "Battlecruiser",  // 225
    "Civilian",  // 226
    "Dropship",  // 227
    "Firebat",  // 228
    "Ghost",  // 229
    "Ghost Remnants",  // 230
    "Nuke Target Dot",  // 231
    "Goliath Base",  // 232
    "Goliath Turret",  // 233
    "Sarah Kerrigan",  // 234
    "Marine",  // 235
    "Marine Remnants",  // 236
    "Scanner Sweep",  // 237
    "Wraith",  // 238
    "SCV",  // 239
    "Siege Tank (Tank) Base",  // 240
    "Siege Tank (Tank) Turret",  // 241
    "Siege Tank (Siege) Base",  // 242
    "Siege Tank (Siege) Turret",  // 243
    "Vulture",  // 244
    "Spider Mine",  // 245
    "Science Vessel (Base)",  // 246
    "Science Vessel (Turret)",  // 247
    "Terran Academy",  // 248
    "Barracks",  // 249
    "Armory",  // 250
    "Comsat Station",  // 251
    "Command Center",  // 252
    "Supply Depot",  // 253
    "Control Tower",  // 254
    "Factory",  // 255
    "Covert Ops",  // 256
    "Ion Cannon",  // 257
    "Machine Shop",  // 258
    "Missile Turret (Base)",  // 259
    "Crashed Battlecruiser",  // 260
    "Physics Lab",  // 261
    "Bunker",  // 262
    "Refinery",  // 263
    "Science Facility",  // 264
    "Nuclear Silo",  // 265
    "Nuclear Missile",  // 266
    "Nuke Hit",  // 267
    "Starport",  // 268
    "Engineering Bay",  // 269
    "Terran Construction (Large)",  // 270
    "Terran Construction (Small)",  // 271
    "Building Explosion (Large)",  // 272
    "Terran Building Rubble (Small)",  // 273
    "Terran Building Rubble (Large)",  // 274
    "Vespene Geyser",  // 275
    "Ragnasaur (Ash)",  // 276
    "Rhynadon (Badlands)",  // 277
    "Bengalaas (Jungle)",  // 278
    "Mineral Field Type1",  // 279
    "Mineral Field Type2",  // 280
    "Mineral Field Type3",  // 281
    "Independent Starport (Unused)",  // 282
    "Zerg Beacon",  // 283
    "Terran Beacon",  // 284
    "Protoss Beacon",  // 285
    "Dark Swarm",  // 286
    "Flag",  // 287
    "Young Chrysalis",  // 288
    "Psi Emitter",  // 289
    "Data Disc",  // 290
    "Khaydarin Crystal",  // 291
    "Mineral Chunk Type1",  // 292
    "Mineral Chunk Type2",  // 293
    "Protoss Gas Orb Type1",  // 294
    "Protoss Gas Orb Type2",  // 295
    "Zerg Gas Sac Type1",  // 296
    "Zerg Gas Sac Type2",  // 297
    "Terran Gas Tank Type1",  // 298
    "Terran Gas Tank Type2",  // 299
    "White Circle (Invisible)",  // 300
    "Start Location",  // 301
    "Map Revealer",  // 302
    "Floor Gun Trap",  // 303
    "Wall Missile Trap",  // 304
    "Wall Missile Trap2",  // 305
    "Wall Flame Trap",  // 306
    "Wall Flame Trap2",  // 307
    "Floor Missile Trap",  // 308
    "Longbolt/Gemini Missiles Trail",  // 309
    "Grenade Shot Smoke",  // 310
    "Vespene Geyser Smoke1",  // 311
    "Vespene Geyser Smoke2",  // 312
    "Vespene Geyser Smoke3",  // 313
    "Vespene Geyser Smoke4",  // 314
    "Vespene Geyser Smoke5",  // 315
    "Small Explosion (Unused)",  // 316
    "Double Explosion",  // 317
    "Cursor Marker",  // 318
    "Egg Spawn",  // 319
    "High Templar Glow",  // 320
    "Psi Field (Right Upper)",  // 321
    "Burrowing Dust",  // 322
    "Building Landing Dust Type1",  // 323
    "Building Landing Dust Type2",  // 324
    "Building Landing Dust Type3",  // 325
    "Building Landing Dust Type4",  // 326
    "Building Landing Dust Type5",  // 327
    "Building Lifting Dust Type1",  // 328
    "Building Lifting Dust Type2",  // 329
    "Building Lifting Dust Type3",  // 330
    "Building Lifting Dust Type4",  // 331
    "Needle Spines",  // 332
    "Dual Photon Blasters Hit",  // 333
    "Particle Beam Hit",  // 334
    "Anti-Matter Missile",  // 335
    "Pulse Cannon",  // 336
    "Phase Disruptor",  // 337
    "STA/STS Photon Cannon Overlay",  // 338
    "Psionic Storm",  // 339
    "Fusion Cutter Hit",  // 340
    "Gauss Rifle Hit",  // 341
    "Gemini Missiles",  // 342
    "Fragmentation Grenade",  // 343
    "Magna Pulse (Unused)",  // 344
    "Lockdown/LongBolt/Hellfire Missile",  // 345
    "C-10 Canister Rifle Hit",  // 346
    "ATS/ATA Laser Battery",  // 347
    "Burst Lasers",  // 348
    "Arclite Shock Cannon Hit",  // 349
    "Yamato Gun",  // 350
    "Yamato Gun Trail",  // 351
    "EMP Shockwave Missile",  // 352
    "Needle Spine Hit",  // 353
    "Plasma Drip Hit (Unused)",  // 354
    "Sunken Colony Tentacle",  // 355
    "Venom (Unused Zerg Weapon)",  // 356
    "Acid Spore",  // 357
    "Glave Wurm",  // 358
    "Seeker Spores",  // 359
    "Queen Spell Holder",  // 360
    "Stasis Field Hit",  // 361
    "Plague Cloud",  // 362
    "Consume",  // 363
    "Ensnare",  // 364
    "Glave Wurm/Seeker Spores Hit",  // 365
    "Psionic Shockwave Hit",  // 366
    "Glave Wurm Trail",  // 367
    "Seeker Spores Overlay",  // 368
    "Phase Disruptor (Unused)",  // 369
    "White Circle",  // 370
    "Acid Spray (Unused)",  // 371
    "Plasma Drip (Unused)",  // 372
    "Scarab/Anti-Matter Missile Overlay",  // 373
    "Hallucination Death1",  // 374
    "Hallucination Death2",  // 375
    "Hallucination Death3",  // 376
    "Bunker Overlay",  // 377
    "FlameThrower",  // 378
    "Recall Field",  // 379
    "Scanner Sweep Hit",  // 380
    "Left Upper Level Door",  // 381
    "Right Upper Level Door",  // 382
    "Substructure Left Door",  // 383
    "Substructure Right Door",  // 384
    "Substructure Opening Hole",  // 385
    "7/13 Twilight",  // 386
    "7/14 Twilight",  // 387
    "7/16 Twilight",  // 388
    "7/15 Twilight",  // 389
    "7/19 Twilight",  // 390
    "7/20 Twilight",  // 391
    "7/21 Twilight",  // 392
    "Unknown Twilight",  // 393
    "7/17 Twilight",  // 394
    "6/1 Twilight",  // 395
    "6/2 Twilight",  // 396
    "6/3 Twilight",  // 397
    "6/4 Twilight",  // 398
    "6/5 Twilight",  // 399
    "8/3 Twilight",  // 400
    "8/4 Twilight",  // 401
    "9/29 Ice",  // 402
    "9/28 Ice",  // 403
    "12/38 Ice",  // 404
    "12/37 Ice",  // 405
    "12/33 Ice",  // 406
    "9/21 Ice",  // 407
    "9/15 Ice",  // 408
    "9/16 Ice",  // 409
    "Unknown410",  // 410
    "Unknown411",  // 411
    "12/9 Ice1",  // 412
    "12/10 Ice",  // 413
    "9/24 Ice",  // 414
    "9/23 Ice",  // 415
    "Unknown416",  // 416
    "12/7 Ice",  // 417
    "12/8 Ice",  // 418
    "12/9 Ice2",  // 419
    "12/11 Ice",  // 420
    "12/40 Ice",  // 421
    "12/41 Ice",  // 422
    "12/42 Ice",  // 423
    "12/5 Ice",  // 424
    "12/6 Ice",  // 425
    "12/36 Ice",  // 426
    "12/32 Ice",  // 427
    "12/35 Ice",  // 428
    "12/34 Ice",  // 429
    "12/24 Ice",  // 430
    "12/25 Ice",  // 431
    "9/22 Ice",  // 432
    "12/31 Ice",  // 433
    "12/20 Ice",  // 434
    "12/26 Ice",  // 435
    "12/27 Ice",  // 436
    "12/30 Ice",  // 437
    "12/39 Ice",  // 438
    "Unknown439",  // 439
    "4/1 Ice",  // 440
    "6/1 Ice",  // 441
    "5/6 Ice",  // 442
    "5/7 Ice",  // 443
    "5/8 Ice",  // 444
    "5/9 Ice",  // 445
    "10/10 Desert1",  // 446
    "10/12 Desert1",  // 447
    "10/8 Desert",  // 448
    "10/9 Desert1",  // 449
    "6/10 Desert",  // 450
    "6/13 Desert",  // 451
    "Unknown Desert",  // 452
    "10/12 Desert2",  // 453
    "10/9 Desert2",  // 454
    "10/10 Desert2",  // 455
    "10/11 Desert",  // 456
    "10/14 Desert",  // 457
    "10/41 Desert",  // 458
    "10/39 Desert",  // 459
    "10/9 Desert",  // 460
    "10/6 Desert",  // 461
    "10/7 Desert",  // 462
    "4/6 Desert",  // 463
    "4/11 Desert",  // 464
    "4/10 Desert",  // 465
    "4/9 Desert",  // 466
    "4/7 Desert",  // 467
    "4/12 Desert",  // 468
    "4/8 Desert",  // 469
    "4/13 Desert",  // 470
    "6/14 Desert",  // 471
    "4/17 Desert",  // 472
    "6/20 Desert",  // 473
    "4/15 Desert1",  // 474
    "4/15 Desert2",  // 475
    "10/23 Desert",  // 476
    "8/23 Desert",  // 477
    "10/5 Desert",  // 478
    "12/1 Desert Overlay",  // 479
    "11/3 Desert",  // 480
    "Lurker Egg",  // 481
    "Devourer",  // 482
    "Devourer Death",  // 483
    "Lurker Remnants",  // 484
    "Lurker",  // 485
    "Dark Archon Energy",  // 486
    "Corsair",  // 487
    "Dark Templar (Unit)",  // 488
    "Medic",  // 489
    "Medic Remnants",  // 490
    "Valkyrie",  // 491
    "Scantid (Desert)",  // 492
    "Kakaru (Twilight)",  // 493
    "Ursadon (Ice)",  // 494
    "Overmind Cocoon",  // 495
    "Power Generator",  // 496
    "Xel'Naga Temple",  // 497
    "Psi Disrupter",  // 498
    "Warp Gate",  // 499
    "Feedback Hit (Small)",  // 500
    "Feedback Hit (Medium)",  // 501
    "Feedback Hit (Large)",  // 502
    "Disruption Web",  // 503
    "White Circle2",  // 504
    "Halo Rockets Trail",  // 505
    "Neutron Flare",  // 506
    "Neutron Flare Overlay (Unused)",  // 507
    "Optical Flare Grenade",  // 508
    "Halo Rockets",  // 509
    "Subterranean Spines Hit",  // 510
    "Subterranean Spines",  // 511
    "Corrosive Acid Shot",  // 512
    "Corrosive Acid Hit",  // 513
    "Maelstrom Hit",  // 514
    "Uraj",  // 515
    "Khalis",  // 516
};
static const char* const kEudImageNames[] = {
    "Scourge",  // 0
    "Scourge Shadow",  // 1
    "Scourge Birth",  // 2
    "Scourge Death",  // 3
    "Scourge Explosion",  // 4
    "Broodling",  // 5
    "Broodling Shadow",  // 6
    "Broodling Remnants",  // 7
    "Infested Terran",  // 8
    "Infested Terran Shadow",  // 9
    "Infested Terran Explosion",  // 10
    "Guardian Cocoon",  // 11
    "Guardian Cocoon Shadow",  // 12
    "Defiler",  // 13
    "Defiler Shadow",  // 14
    "Defiler Birth",  // 15
    "Defiler Remnants",  // 16
    "Drone",  // 17
    "Drone Shadow",  // 18
    "Drone Birth",  // 19
    "Drone Remnants",  // 20
    "Egg",  // 21
    "Egg Shadow",  // 22
    "Egg Spawn",  // 23
    "Egg Remnants",  // 24
    "Guardian",  // 25
    "Guardian Shadow",  // 26
    "Guardian Birth",  // 27
    "Guardian Death",  // 28
    "Hydralisk",  // 29
    "Hydralisk Shadow",  // 30
    "Hydralisk Birth",  // 31
    "Hydralisk Remnants",  // 32
    "Infested Kerrigan",  // 33
    "Infested Kerrigan Shadow",  // 34
    "Needle Spines",  // 35
    "Larva",  // 36
    "Larva Remnants",  // 37
    "Mutalisk",  // 38
    "Mutalisk Shadow",  // 39
    "Mutalisk Birth",  // 40
    "Mutalisk Death",  // 41
    "Overlord",  // 42
    "Overlord Shadow",  // 43
    "Overlord Birth",  // 44
    "Overlord Death",  // 45
    "Queen",  // 46
    "Queen Shadow",  // 47
    "Queen Death",  // 48
    "Queen Birth",  // 49
    "Ultralisk",  // 50
    "Ultralisk Shadow",  // 51
    "Ultralisk Birth",  // 52
    "Ultralisk Remnants",  // 53
    "Zergling",  // 54
    "Zergling Shadow",  // 55
    "Zergling Birth",  // 56
    "Zergling Remnants",  // 57
    "Zerg Air Death Explosion (Large)",  // 58
    "Zerg Air Death Explosion (Small)",  // 59
    "Zerg Building Explosion",  // 60
    "Cerebrate",  // 61
    "Cerebrate Shadow",  // 62
    "Infested Command Center",  // 63
    "Spawning Pool",  // 64
    "Spawning Pool Shadow",  // 65
    "Evolution Chamber",  // 66
    "Evolution Chamber Shadow",  // 67
    "Creep Colony",  // 68
    "Creep Colony Shadow",  // 69
    "Hatchery",  // 70
    "Hatchery Shadow",  // 71
    "Hive",  // 72
    "Hive Shadow",  // 73
    "Lair",  // 74
    "Lair Shadow",  // 75
    "Sunken Colony",  // 76
    "Sunken Colony Shadow",  // 77
    "Mature Chrysalis",  // 78
    "Mature Chrysalis Shadow",  // 79
    "Greater Spire",  // 80
    "Greater Spire Shadow",  // 81
    "Defiler Mound",  // 82
    "Defiler Mound Shadow",  // 83
    "Queen's Nest",  // 84
    "Queen's Nest Shadow",  // 85
    "Nydus Canal",  // 86
    "Nydus Canal Shadow",  // 87
    "Overmind With Shell",  // 88
    "Overmind Remnants",  // 89
    "Overmind Without Shell",  // 90
    "Ultralisk Cavern",  // 91
    "Ultralisk Cavern Shadow",  // 92
    "Extractor",  // 93
    "Extractor Shadow",  // 94
    "Hydralisk Den",  // 95
    "Hydralisk Den Shadow",  // 96
    "Spire",  // 97
    "Spire Shadow",  // 98
    "Spore Colony",  // 99
    "Spore Colony Shadow",  // 100
    "Infested Command Center Overlay",  // 101
    "Zerg Construction (Large)",  // 102
    "Zerg Building Morph",  // 103
    "Zerg Construction (Medium)",  // 104
    "Zerg Construction (Small)",  // 105
    "Zerg Building Construction Shadow",  // 106
    "Zerg Building Spawn (Small)",  // 107
    "Zerg Building Spawn (Medium)",  // 108
    "Zerg Building Spawn (Large)",  // 109
    "Zerg Building Rubble (Small)",  // 110
    "Zerg Building Rubble (Large)",  // 111
    "Carrier",  // 112
    "Carrier Shadow",  // 113
    "Carrier Engines",  // 114
    "Carrier Warp Flash",  // 115
    "Interceptor",  // 116
    "Interceptor Shadow",  // 117
    "Shuttle",  // 118
    "Shuttle Shadow",  // 119
    "Shuttle Engines",  // 120
    "Shuttle Warp Flash",  // 121
    "Dragoon",  // 122
    "Dragoon Shadow",  // 123
    "Dragoon Remnants",  // 124
    "Dragoon Warp Flash",  // 125
    "High Templar",  // 126
    "High Templar Shadow",  // 127
    "High Templar Warp Flash",  // 128
    "Dark Templar (Hero)",  // 129
    "Arbiter",  // 130
    "Arbiter Shadow",  // 131
    "Arbiter Engines",  // 132
    "Arbiter Warp Flash",  // 133
    "Archon Energy",  // 134
    "Archon Being",  // 135
    "Archon Swirl",  // 136
    "Probe",  // 137
    "Probe Shadow",  // 138
    "Probe Warp Flash",  // 139
    "Scout",  // 140
    "Scout Shadow",  // 141
    "Scout Engines",  // 142
    "Scout Warp Flash",  // 143
    "Reaver",  // 144
    "Reaver Shadow",  // 145
    "Reaver Warp Flash",  // 146
    "Scarab",  // 147
    "Observer",  // 148
    "Observer Shadow",  // 149
    "Observer Warp Flash",  // 150
    "Zealot",  // 151
    "Zealot Shadow",  // 152
    "Zealot Death",  // 153
    "Zealot Warp Flash",  // 154
    "Templar Archives",  // 155
    "Templar Archives Warp Flash",  // 156
    "Templar Archives Shadow",  // 157
    "Assimilator",  // 158
    "Assimilator Warp Flash",  // 159
    "Assimilator Shadow",  // 160
    "Observatory",  // 161
    "Observatory Warp Flash",  // 162
    "Observatory Shadow",  // 163
    "Citadel of Adun",  // 164
    "Citadel of Adun Warp Flash",  // 165
    "Citadel of Adun Shadow",  // 166
    "Forge",  // 167
    "Forge Overlay",  // 168
    "Forge Warp Flash",  // 169
    "Forge Shadow",  // 170
    "Gateway",  // 171
    "Gateway Warp Flash",  // 172
    "Gateway Shadow",  // 173
    "Cybernetics Core",  // 174
    "Cybernetics Core Warp Flash",  // 175
    "Cybernetics Core Overlay",  // 176
    "Cybernetics Core Shadow",  // 177
    "Khaydarin Crystal Formation",  // 178
    "Nexus",  // 179
    "Nexus Warp Flash",  // 180
    "Nexus Overlay",  // 181
    "Nexus Shadow",  // 182
    "Photon Cannon",  // 183
    "Photon Cannon Shadow",  // 184
    "Photon Cannon Warp Flash",  // 185
    "Arbiter Tribunal",  // 186
    "Arbiter Tribunal Warp Flash",  // 187
    "Arbiter Tribunal Shadow",  // 188
    "Pylon",  // 189
    "Pylon Warp Flash",  // 190
    "Pylon Shadow",  // 191
    "Robotics Facility",  // 192
    "Robotics Facility Warp Flash",  // 193
    "Robotics Facility Shadow",  // 194
    "Shield Battery",  // 195
    "Shield Battery Overlay",  // 196
    "Shileld Battery Warp Flash",  // 197
    "Shield Battery Shadow",  // 198
    "Stargate",  // 199
    "Stargate Overlay",  // 200
    "Stargate Warp Flash",  // 201
    "Stargate Shadow",  // 202
    "Stasis Cell/Prison",  // 203
    "Robotics Support Bay",  // 204
    "Robotics Support Bay Warp Flash",  // 205
    "Robotics Support Bay Shadow",  // 206
    "Protoss Temple",  // 207
    "Fleet Beacon",  // 208
    "Fleet Beacon Warp Flash",  // 209
    "Warp Texture",  // 210
    "Warp Anchor",  // 211
    "Fleet Beacon Shadow",  // 212
    "Explosion1 (Small)",  // 213
    "Explosion1 (Medium)",  // 214
    "Explosion (Large)",  // 215
    "Protoss Building Rubble (Small)",  // 216
    "Protoss Building Rubble (Large)",  // 217
    "Battlecruiser",  // 218
    "Battlecruiser Shadow",  // 219
    "Battlecruiser Engines",  // 220
    "Civilian",  // 221
    "Civilian Shadow",  // 222
    "Dropship",  // 223
    "Dropship Shadow",  // 224
    "Dropship Engines",  // 225
    "Firebat",  // 226
    "Firebat Shadow",  // 227
    "Ghost",  // 228
    "Ghost Shadow",  // 229
    "Ghost Remnants",  // 230
    "Ghost Death",  // 231
    "Nuke Beam",  // 232
    "Nuke Target Dot",  // 233
    "Goliath Base",  // 234
    "Goliath Turret",  // 235
    "Goliath Shadow",  // 236
    "Sarah Kerrigan",  // 237
    "Sarah Kerrigan Shadow",  // 238
    "Marine",  // 239
    "Marine Shadow",  // 240
    "Marine Remnants",  // 241
    "Marine Death",  // 242
    "Wraith",  // 243
    "Wraith Shadow",  // 244
    "Wraith Engines",  // 245
    "Scanner Sweep",  // 246
    "SCV",  // 247
    "SCV Shadow",  // 248
    "SCV Glow",  // 249
    "Siege Tank (Tank) Base",  // 250
    "Siege Tank (Tank) Turret",  // 251
    "Siege Tank (Tank) Base Shadow",  // 252
    "Siege Tank (Siege) Base",  // 253
    "Siege Tank (Siege) Turret",  // 254
    "Siege Tank (Siege) Base Shadow",  // 255
    "Vulture",  // 256
    "Vulture Shadow",  // 257
    "Spider Mine",  // 258
    "Spider Mine Shadow",  // 259
    "Science Vessel (Base)",  // 260
    "Science Vessel (Turret)",  // 261
    "Science Vessel Shadow",  // 262
    "Terran Academy",  // 263
    "Academy Overlay",  // 264
    "Academy Shadow",  // 265
    "Barracks",  // 266
    "Barracks Shadow",  // 267
    "Armory",  // 268
    "Armory Overlay",  // 269
    "Armory Shadow",  // 270
    "Comsat Station",  // 271
    "Comsat Station Connector",  // 272
    "Comsat Station Overlay",  // 273
    "Comsat Station Shadow",  // 274
    "Command Center",  // 275
    "Command Center Overlay",  // 276
    "Command Center Shadow",  // 277
    "Supply Depot",  // 278
    "Supply Depot Overlay",  // 279
    "Supply Depot Shadow",  // 280
    "Control Tower",  // 281
    "Control Tower Connector",  // 282
    "Control Tower Overlay",  // 283
    "Control Tower Shadow",  // 284
    "Factory",  // 285
    "Factory Overlay",  // 286
    "Factory Shadow",  // 287
    "Covert Ops",  // 288
    "Covert Ops Connector",  // 289
    "Covert Ops Overlay",  // 290
    "Covert Ops Shadow",  // 291
    "Ion Cannon",  // 292
    "Machine Shop",  // 293
    "Machine Shop Connector",  // 294
    "Machine Shop Shadow",  // 295
    "Missile Turret (Base)",  // 296
    "Missile Turret (Turret)",  // 297
    "Missile Turret (Base) Shadow",  // 298
    "Crashed Battlecruiser",  // 299
    "Crashed Battlecruiser Shadow",  // 300
    "Physics Lab",  // 301
    "Physics Lab Connector",  // 302
    "Physics Lab Shadow",  // 303
    "Bunker",  // 304
    "Bunker Shadow",  // 305
    "Bunker Overlay",  // 306
    "Refinery",  // 307
    "Refinery Shadow",  // 308
    "Science Facility",  // 309
    "Science Facility Overlay",  // 310
    "Science Facility Shadow",  // 311
    "Nuclear Silo",  // 312
    "Nuclear Silo Connector",  // 313
    "Nuclear Silo Overlay",  // 314
    "Nuclear Silo Shadow",  // 315
    "Nuclear Missile",  // 316
    "Nuclear Missile Shadow",  // 317
    "Nuke Hit",  // 318
    "Starport",  // 319
    "Starport Overlay",  // 320
    "Starport Shadow",  // 321
    "Engineering Bay",  // 322
    "Engineering Bay Overlay",  // 323
    "Engineering Bay Shadow",  // 324
    "Terran Construction (Large)",  // 325
    "Terran Construction (Large) Shadow",  // 326
    "Terran Construction (Medium)",  // 327
    "Terran Construction (Medium) Shadow",  // 328
    "Addon Construction",  // 329
    "Terran Construction (Small)",  // 330
    "Terran Construction (Small) Shadow",  // 331
    "Explosion2 (Small)",  // 332
    "Explosion2 (Medium)",  // 333
    "Building Explosion (Large)",  // 334
    "Terran Building Rubble (Small)",  // 335
    "Terran Building Rubble (Large)",  // 336
    "Dark Swarm",  // 337
    "Ragnasaur (Ash)",  // 338
    "Ragnasaur (Ash) Shadow",  // 339
    "Rhynadon (Badlands)",  // 340
    "Rhynadon (Badlands) Shadow",  // 341
    "Bengalaas (Jungle)",  // 342
    "Bengalaas (Jungle) Shadow",  // 343
    "Vespene Geyser",  // 344
    "Vespene Geyser2",  // 345
    "Vespene Geyser Shadow",  // 346
    "Mineral Field Type1",  // 347
    "Mineral Field Type1 Shadow",  // 348
    "Mineral Field Type2",  // 349
    "Mineral Field Type2 Shadow",  // 350
    "Mineral Field Type3",  // 351
    "Mineral Field Type3 Shadow",  // 352
    "Independent Starport (Unused)",  // 353
    "Zerg Beacon",  // 354
    "Zerg Beacon Overlay",  // 355
    "Terran Beacon",  // 356
    "Terran Beacon Overlay",  // 357
    "Protoss Beacon",  // 358
    "Protoss Beacon Overlay",  // 359
    "Magna Pulse (Unused)",  // 360
    "Lockdown Field (Small)",  // 361
    "Lockdown Field (Medium)",  // 362
    "Lockdown Field (Large)",  // 363
    "Stasis Field Hit",  // 364
    "Stasis Field (Small)",  // 365
    "Stasis Field (Medium)",  // 366
    "Stasis Field (Large)",  // 367
    "Recharge Shields (Small)",  // 368
    "Recharge Shields (Medium)",  // 369
    "Recharge Shields (Large)",  // 370
    "Defensive Matrix Front (Small)",  // 371
    "Defensive Matrix Front (Medium)",  // 372
    "Defensive Matrix Front (Large)",  // 373
    "Defensive Matrix Back (Small)",  // 374
    "Defensive Matrix Back (Medium)",  // 375
    "Defensive Matrix Back (Large)",  // 376
    "Defensive Matrix Hit (Small)",  // 377
    "Defensive Matrix Hit (Medium)",  // 378
    "Defensive Matrix Hit (Large)",  // 379
    "Irradiate (Small)",  // 380
    "Irradiate (Medium)",  // 381
    "Irradiate (Large)",  // 382
    "Ensnare Cloud",  // 383
    "Ensnare Overlay (Small)",  // 384
    "Ensnare Overlay (Medium)",  // 385
    "Ensnare Overlay (Large)",  // 386
    "Plague Cloud",  // 387
    "Plague Overlay (Small)",  // 388
    "Plague Overlay (Medium)",  // 389
    "Plague Overlay (Large)",  // 390
    "Recall Field",  // 391
    "Flag",  // 392
    "Young Chrysalis",  // 393
    "Psi Emitter",  // 394
    "Data Disc",  // 395
    "Khaydarin Crystal",  // 396
    "Mineral Chunk Type1",  // 397
    "Mineral Chunk Type2",  // 398
    "Protoss Gas Orb Type1",  // 399
    "Protoss Gas Orb Type2",  // 400
    "Zerg Gas Sac Type1",  // 401
    "Zerg Gas Sac Type2",  // 402
    "Terran Gas Tank Type1",  // 403
    "Terran Gas Tank Type2",  // 404
    "Mineral Chunk Shadow",  // 405
    "Protoss Gas Orb Shadow",  // 406
    "Zerg Gas Sac Shadow",  // 407
    "Terran Gas Tank Shadow",  // 408
    "Data Disk Shadow (Ground)",  // 409
    "Data Disk Shadow (Carried)",  // 410
    "Flag Shadow (Ground)",  // 411
    "Flag Shadow (Carried)",  // 412
    "Crystal Shadow (Ground)",  // 413
    "Crystal Shadow (Carried)",  // 414
    "Young Chrysalis Shadow (Ground)",  // 415
    "Young Chrysalis Shadow (Carried)",  // 416
    "Psi Emitter Shadow (Ground)",  // 417
    "Psi Emitter Shadow (Carried)",  // 418
    "Acid Spray (Unused)",  // 419
    "Plasma Drip (Unused)",  // 420
    "FlameThrower",  // 421
    "Longbolt/Gemini Missiles Trail",  // 422
    "Burrowing Dust",  // 423
    "Shield Overlay",  // 424
    "Small Explosion (Unused)",  // 425
    "Double Explosion",  // 426
    "Phase Disruptor Hit",  // 427
    "Nuclear Missile Death",  // 428
    "Spider Mine Death",  // 429
    "Vespene Geyser Smoke1",  // 430
    "Vespene Geyser Smoke2",  // 431
    "Vespene Geyser Smoke3",  // 432
    "Vespene Geyser Smoke4",  // 433
    "Vespene Geyser Smoke5",  // 434
    "Vespene Geyser Smoke1 Overlay",  // 435
    "Vespene Geyser Smoke2 Overlay",  // 436
    "Vespene Geyser Smoke3 Overlay",  // 437
    "Vespene Geyser Smoke4 Overlay",  // 438
    "Vespene Geyser Smoke5 Overlay",  // 439
    "Fragmentation Grenade Hit",  // 440
    "Grenade Shot Smoke",  // 441
    "Anti-Matter Missile Hit",  // 442
    "Scarab/Anti-Matter Missile Overlay",  // 443
    "Scarab Hit",  // 444
    "Cursor Marker",  // 445
    "Battlecruiser Attack Overlay",  // 446
    "Burst Lasers Hit",  // 447
    "Burst Lasers Overlay (Unused)",  // 448
    "High Templar Glow",  // 449
    "Flames1 Type1 (Small)",  // 450
    "Flames1 Type2 (Small)",  // 451
    "Flames1 Type3 (Small)",  // 452
    "Flames2 Type3 (Small)",  // 453
    "Flames3 Type3 (Small)",  // 454
    "Flames4 Type3 (Small)",  // 455
    "Flames5 Type3 (Small)",  // 456
    "Flames6 Type3 (Small)",  // 457
    "Bleeding Variant1 Type1 (Small)",  // 458
    "Bleeding Variant1 Type2 (Small)",  // 459
    "Bleeding Variant1 Type3 (Small)",  // 460
    "Bleeding Variant1 Type4 (Small)",  // 461
    "Bleeding Variant2 Type1 (Small)",  // 462
    "Bleeding Variant2 Type2 (Small)",  // 463
    "Bleeding Variant2 Type3 (Small)",  // 464
    "Bleeding Variant2 Type4 (Small)",  // 465
    "Flames2 Type1 (Small)",  // 466
    "Flames2 Type2 (Small)",  // 467
    "Flames7 Type3 (Small)",  // 468
    "Flames3 Type1 (Small)",  // 469
    "Flames3 Type2 (Small)",  // 470
    "Flames8 Type3 (Small)",  // 471
    "Flames1 Type1 (Large)",  // 472
    "Flames1 Type2 (Large)",  // 473
    "Flames1 Type3 (Large)",  // 474
    "Flames2 Type3 (Large)",  // 475
    "Flames3 Type3 (Large)",  // 476
    "Flames4 Type3 (Large)",  // 477
    "Flames5 Type3 (Large)",  // 478
    "Flames6 Type3 (Large)",  // 479
    "Bleeding Variant1 Type1 (Large)",  // 480
    "Bleeding Variant1 Type2 (Large)",  // 481
    "Bleeding Variant1 Type3 (Large)",  // 482
    "Bleeding Variant1 Type4 (Large)",  // 483
    "Bleeding Variant2 Type1 (Large)",  // 484
    "Bleeding Variant2 Type3 (Large)",  // 485
    "Bleeding Variant3 Type3 (Large)",  // 486
    "Bleeding Variant2 Type4 (Large)",  // 487
    "Flames2 Type1 (Large)",  // 488
    "Flames2 Type2 (Large)",  // 489
    "Flames7 Type3 (Large)",  // 490
    "Flames3 Type1 (Large)",  // 491
    "Flames3 Type2 (Large)",  // 492
    "Flames8 Type3 (Large)",  // 493
    "Building Landing Dust Type1",  // 494
    "Building Landing Dust Type2",  // 495
    "Building Landing Dust Type3",  // 496
    "Building Landing Dust Type4",  // 497
    "Building Landing Dust Type5",  // 498
    "Building Lifting Dust Type1",  // 499
    "Building Lifting Dust Type2",  // 500
    "Building Lifting Dust Type3",  // 501
    "Building Lifting Dust Type4",  // 502
    "White Circle",  // 503
    "Needle Spine Hit",  // 504
    "Plasma Drip Hit (Unused)",  // 505
    "Sunken Colony Tentacle",  // 506
    "Venom (Unused Zerg Weapon)",  // 507
    "Venom Hit (Unused)",  // 508
    "Acid Spore",  // 509
    "Acid Spore Hit",  // 510
    "Glave Wurm",  // 511
    "Glave Wurm/Seeker Spores Hit",  // 512
    "Glave Wurm Trail",  // 513
    "Seeker Spores Overlay",  // 514
    "Seeker Spores",  // 515
    "Queen Spell Holder",  // 516
    "Consume",  // 517
    "Guardian Attack Overlay",  // 518
    "Dual Photon Blasters Hit",  // 519
    "Particle Beam Hit",  // 520
    "Anti-Matter Missile",  // 521
    "Pulse Cannon",  // 522
    "Phase Disruptor",  // 523
    "STA/STS Photon Cannon Overlay",  // 524
    "Psionic Storm",  // 525
    "Fusion Cutter Hit",  // 526
    "Gauss Rifle Hit",  // 527
    "Gemini Missiles",  // 528
    "Lockdown/LongBolt/Hellfire Missile",  // 529
    "Gemini Missiles Explosion",  // 530
    "C-10 Canister Rifle Hit",  // 531
    "Fragmentation Grenade",  // 532
    "Arclite Shock Cannon Hit",  // 533
    "ATS/ATA Laser Battery",  // 534
    "Burst Lasers",  // 535
    "Siege Tank(Tank) Turret Attack Overlay",  // 536
    "Siege Tank(Siege) Turret Attack Overlay",  // 537
    "Science Vessel Overlay (Part1)",  // 538
    "Science Vessel Overlay (Part2)",  // 539
    "Science Vessel Overlay (Part3)",  // 540
    "Yamato Gun",  // 541
    "Yamato Gun Trail",  // 542
    "Yamato Gun Overlay",  // 543
    "Yamato Gun Hit",  // 544
    "Hallucination Hit",  // 545
    "Scanner Sweep Hit",  // 546
    "Archon Birth (Unused)",  // 547
    "Psionic Shockwave Hit",  // 548
    "Archon Beam",  // 549
    "Psionic Storm Part1",  // 550
    "Psionic Storm Part2",  // 551
    "Psionic Storm Part3",  // 552
    "Psionic Storm Part4",  // 553
    "EMP Shockwave Missile",  // 554
    "EMP Shockwave Hit (Part1)",  // 555
    "EMP Shockwave Hit (Part2)",  // 556
    "Hallucination Death1",  // 557
    "Hallucination Death2",  // 558
    "Hallucination Death3",  // 559
    "Circle Marker1",  // 560
    "Selection Circle (22pixels)",  // 561
    "Selection Circle (32pixels)",  // 562
    "Selection Circle (48pixels)",  // 563
    "Selection Circle (62pixels)",  // 564
    "Selection Circle (72pixels)",  // 565
    "Selection Circle (94pixels)",  // 566
    "Selection Circle (110pixels)",  // 567
    "Selection Circle (122pixels)",  // 568
    "Selection Circle (146pixels)",  // 569
    "Selection Circle (224pixels)",  // 570
    "Selection Circle Dashed (22pixels)",  // 571
    "Selection Circle Dashed (32pixels)",  // 572
    "Selection Circle Dashed (48pixels)",  // 573
    "Selection Circle Dashed (62pixels)",  // 574
    "Selection Circle Dashed (72pixels)",  // 575
    "Selection Circle Dashed (94pixels)",  // 576
    "Selection Circle Dashed (110pixels)",  // 577
    "Selection Circle Dashed (122pixels)",  // 578
    "Selection Circle Dashed (146pixels)",  // 579
    "Selection Circle Dashed (224pixels)",  // 580
    "Circle Marker2",  // 581
    "Map Revealer",  // 582
    "Circle Marker3",  // 583
    "Psi Field1 (Right Upper)",  // 584
    "Psi Field1 (Right Lower)",  // 585
    "Psi Field2 (Right Upper)",  // 586
    "Psi Field2 (Right Lower)",  // 587
    "Start Location",  // 588
    "2/38 Ash",  // 589
    "2/38 Ash Shadow",  // 590
    "2/39 Ash",  // 591
    "2/39 Ash Shadow",  // 592
    "2/41 Ash",  // 593
    "2/41 Ash Shadow",  // 594
    "2/40 Ash",  // 595
    "2/40 Ash Shadow",  // 596
    "2/42 Ash",  // 597
    "2/42 Ash Shadow",  // 598
    "2/43 Ash",  // 599
    "2/44 Ash",  // 600
    "2/1 Ash",  // 601
    "2/4 Ash",  // 602
    "2/5 Ash",  // 603
    "2/30 Ash",  // 604
    "2/28 Ash",  // 605
    "2/29 Ash",  // 606
    "4/1 Ash",  // 607
    "4/2 Ash",  // 608
    "4/3 Ash",  // 609
    "4/56 Jungle",  // 610
    "4/56 Jungle Shadow",  // 611
    "4/57 Jungle",  // 612
    "4/57 Jungle Shadow",  // 613
    "4/58 Jungle",  // 614
    "4/58 Jungle Shadow",  // 615
    "4/59 Jungle",  // 616
    "4/59 Jungle Shadow",  // 617
    "9/5 Jungle",  // 618
    "9/5 Jungle Shadow",  // 619
    "9/6 Jungle",  // 620
    "9/6 Jungle Shadow",  // 621
    "9/7 Jungle",  // 622
    "9/7 Jungle Shadow",  // 623
    "4/51 Jungle",  // 624
    "4/51 Jungle Shadow",  // 625
    "4/52 Jungle",  // 626
    "4/52 Jungle Shadow",  // 627
    "4/54 Jungle",  // 628
    "4/54 Jungle Shadow",  // 629
    "4/53 Jungle",  // 630
    "4/53 Jungle Shadow",  // 631
    "9/1 Jungle",  // 632
    "9/1 Jungle Shadow",  // 633
    "9/2 Jungle",  // 634
    "9/2 Jungle Shadow",  // 635
    "9/3 Jungle",  // 636
    "9/3 Jungle Shadow",  // 637
    "9/4 Jungle",  // 638
    "9/4 Jungle Shadow",  // 639
    "4/12 Jungle",  // 640
    "4/13 Jungle",  // 641
    "4/1 Jungle",  // 642
    "4/3 Jungle",  // 643
    "4/2 Jungle",  // 644
    "4/5 Jungle",  // 645
    "4/4 Jungle",  // 646
    "4/9 Jungle",  // 647
    "4/10 Jungle",  // 648
    "5/5 Jungle",  // 649
    "5/7 Jungle",  // 650
    "5/6 Jungle",  // 651
    "5/9 Jungle",  // 652
    "5/8 Jungle",  // 653
    "4/6 Jungle",  // 654
    "4/7 Jungle",  // 655
    "4/17 Jungle",  // 656
    "13/4 Jungle",  // 657
    "11/5 Jungle",  // 658
    "11/6 Jungle",  // 659
    "11/7 Jungle",  // 660
    "11/8 Jungle",  // 661
    "11/10 Jungle",  // 662
    "11/11 Jungle",  // 663
    "11/12 Jungle",  // 664
    "7/4 Platform",  // 665
    "7/4 Platform Shadow",  // 666
    "7/5 Platform",  // 667
    "7/5 Platform Shadow",  // 668
    "7/6 Platform",  // 669
    "7/6 Platform Shadow",  // 670
    "7/1 Platform",  // 671
    "7/1 Platform Shadow",  // 672
    "7/2 Platform",  // 673
    "7/2 Platform Shadow",  // 674
    "7/3 Platform",  // 675
    "7/3 Platform Shadow",  // 676
    "7/9 Platform",  // 677
    "7/10 Platform",  // 678
    "7/8 Platform",  // 679
    "7/7 Platform",  // 680
    "7/26 Platform",  // 681
    "7/24 Platform",  // 682
    "7/28 Platform",  // 683
    "7/27 Platform",  // 684
    "7/25 Platform",  // 685
    "7/29 Platform",  // 686
    "7/30 Platform",  // 687
    "7/31 Platform",  // 688
    "12/1 Platform",  // 689
    "9/27 Platform",  // 690
    "5/54 Badlands",  // 691
    "5/54 Badlands Shadow",  // 692
    "5/55 Badlands",  // 693
    "5/55 Badlands Shadow",  // 694
    "5/56 Badlands",  // 695
    "5/56 Badlands Shadow",  // 696
    "5/57 Badlands",  // 697
    "5/57 Badlands Shadow",  // 698
    "6/16 Badlands",  // 699
    "6/17 Badlands",  // 700
    "6/20 Badlands",  // 701
    "6/21 Badlands",  // 702
    "5/10 Badlands",  // 703
    "5/50 Badlands",  // 704
    "5/50 Badlands Shadow",  // 705
    "5/52 Badlands",  // 706
    "5/52 Badlands Shadow",  // 707
    "5/53 Badlands",  // 708
    "5/53 Badlands Shadow",  // 709
    "5/51 Badlands",  // 710
    "5/51 Badlands Shadow",  // 711
    "6/3 Badlands",  // 712
    "11/3 Badlands",  // 713
    "11/8 Badlands",  // 714
    "11/6 Badlands",  // 715
    "11/7 Badlands",  // 716
    "11/9 Badlands",  // 717
    "11/10 Badlands",  // 718
    "11/11 Badlands",  // 719
    "11/12 Badlands",  // 720
    "11/13 Badlands",  // 721
    "11/14 Badlands",  // 722
    "1/13 Badlands",  // 723
    "1/9 Badlands",  // 724
    "1/11 Badlands",  // 725
    "1/14 Badlands",  // 726
    "1/10 Badlands",  // 727
    "1/12 Badlands",  // 728
    "1/15 Badlands",  // 729
    "1/7 Badlands",  // 730
    "1/5 Badlands",  // 731
    "1/16 Badlands",  // 732
    "1/8 Badlands",  // 733
    "1/6 Badlands",  // 734
    "Floor Gun Trap",  // 735
    "Floor Missile Trap",  // 736
    "Floor Missile Trap Turret",  // 737
    "Wall Missile Trap",  // 738
    "Wall Missile Trap2",  // 739
    "Wall Flame Trap",  // 740
    "Wall Flame Trap2",  // 741
    "Left Upper Level Door",  // 742
    "Right Upper Level Door",  // 743
    "4/15 Installation1",  // 744
    "4/15 Installation2",  // 745
    "3/9 Installation",  // 746
    "3/10 Installation",  // 747
    "3/11 Installation",  // 748
    "3/12 Installation",  // 749
    "Substructure Left Door",  // 750
    "Substructure Right Door",  // 751
    "3/1 Installation",  // 752
    "3/2 Installation",  // 753
    "Substructure Opening Hole",  // 754
    "7/21 Twilight",  // 755
    "Unknown Twilight",  // 756
    "7/13 Twilight",  // 757
    "7/14 Twilight",  // 758
    "7/16 Twilight",  // 759
    "7/15 Twilight",  // 760
    "7/19 Twilight",  // 761
    "7/20 Twilight",  // 762
    "7/17 Twilight",  // 763
    "6/1 Twilight",  // 764
    "6/2 Twilight",  // 765
    "6/3 Twilight",  // 766
    "6/4 Twilight",  // 767
    "6/5 Twilight",  // 768
    "8/3 Twilight1",  // 769
    "8/3 Twilight2",  // 770
    "9/29 Ice",  // 771
    "9/29 Ice Shadow",  // 772
    "9/28 Ice",  // 773
    "9/28 Ice Shadow",  // 774
    "12/38 Ice",  // 775
    "12/38 Ice Shadow",  // 776
    "12/37 Ice",  // 777
    "12/37 Ice Shadow",  // 778
    "12/33 Ice1",  // 779
    "12/33 Ice1 Shadow",  // 780
    "9/21 Ice",  // 781
    "9/21 Ice Shadow",  // 782
    "9/15 Ice",  // 783
    "9/15 Ice Shadow",  // 784
    "9/16 Ice",  // 785
    "9/16 Ice Shadow",  // 786
    "Unknown787",  // 787
    "Unknown788",  // 788
    "12/9 Ice1",  // 789
    "12/10 Ice1",  // 790
    "9/24 Ice",  // 791
    "9/24 Ice Shadow",  // 792
    "9/23 Ice",  // 793
    "9/23 Ice Shadow",  // 794
    "Unknown795",  // 795
    "Unknown Ice Shadow",  // 796
    "12/7 Ice",  // 797
    "12/7 Ice Shadow",  // 798
    "12/8 Ice",  // 799
    "12/8 Ice Shadow",  // 800
    "12/9 Ice2",  // 801
    "12/9 Ice2 Shadow",  // 802
    "12/10 Ice2",  // 803
    "12/10 Ice2 Shadow",  // 804
    "12/40 Ice",  // 805
    "12/40 Ice Shadow",  // 806
    "12/41 Ice",  // 807
    "12/41 Ice Shadow",  // 808
    "12/42 Ice",  // 809
    "12/42 Ice Shadow",  // 810
    "12/5 Ice",  // 811
    "12/5 Ice Shadow",  // 812
    "12/6 Ice",  // 813
    "12/6 Ice Shadow",  // 814
    "12/36 Ice",  // 815
    "12/36 Ice Shadow",  // 816
    "12/32 Ice",  // 817
    "12/32 Ice Shadow",  // 818
    "12/33 Ice2",  // 819
    "12/33 Ice2 Shadow",  // 820
    "12/34 Ice",  // 821
    "12/34 Ice Shadow",  // 822
    "12/24 Ice1",  // 823
    "12/24 Ice1 Shadow",  // 824
    "12/25 Ice1",  // 825
    "12/25 Ice1 Shadow",  // 826
    "12/30 Ice1",  // 827
    "12/30 Ice1 Shadow",  // 828
    "12/31 Ice",  // 829
    "12/31 Ice Shadow",  // 830
    "12/20 Ice",  // 831
    "12/30 Ice2",  // 832
    "12/30 Ice2 Shadow",  // 833
    "9/22 Ice",  // 834
    "9/22 Ice Shadow",  // 835
    "12/24 Ice2",  // 836
    "12/24 Ice2 Shadow",  // 837
    "12/25 Ice2",  // 838
    "12/25 Ice2 Shadow",  // 839
    "Unknown840",  // 840
    "4/1 Ice",  // 841
    "6/1 Ice",  // 842
    "5/6 Ice",  // 843
    "5/6 Ice Shadow",  // 844
    "5/7 Ice",  // 845
    "5/7 Ice Shadow",  // 846
    "5/8 Ice",  // 847
    "5/8 Ice Shadow",  // 848
    "5/9 Ice",  // 849
    "5/9 Ice Shadow",  // 850
    "10/10 Desert1",  // 851
    "10/12 Desert1",  // 852
    "10/12 Desert1 Shadow",  // 853
    "10/8 Desert1",  // 854
    "10/8 Desert1 Shadow",  // 855
    "10/9 Desert1",  // 856
    "10/9 Desert1 Shadow",  // 857
    "6/10 Desert",  // 858
    "6/10 Desert Shadow",  // 859
    "6/13 Desert1",  // 860
    "6/13 Desert1 Shadow",  // 861
    "Unknown Desert",  // 862
    "Unknown Desert Shadow",  // 863
    "10/12 Desert2",  // 864
    "10/12 Desert2 Shadow",  // 865
    "10/9 Desert2",  // 866
    "10/9 Desert2 Shadow",  // 867
    "10/10 Desert2",  // 868
    "10/10 Desert2 Shadow",  // 869
    "10/11 Desert",  // 870
    "10/11 Desert Shadow",  // 871
    "10/14 Desert",  // 872
    "10/14 Desert Shadow",  // 873
    "10/41 Desert",  // 874
    "10/41 Desert Shadow",  // 875
    "10/39 Desert",  // 876
    "1/39 Desert Shadow",  // 877
    "10/8 Desert2",  // 878
    "10/8 Desert2 Shadow",  // 879
    "10/6 Desert",  // 880
    "10/7 Desert",  // 881
    "10/7 Desert Shadow",  // 882
    "4/6 Desert",  // 883
    "4/6 Desert Shadow",  // 884
    "4/11 Desert",  // 885
    "4/11 Desert Shadow",  // 886
    "4/10 Desert",  // 887
    "4/10 Desert Shadow",  // 888
    "4/9 Desert",  // 889
    "4/7 Desert",  // 890
    "4/7 Desert Shadow",  // 891
    "4/12 Desert",  // 892
    "4/12 Desert Shadow",  // 893
    "4/8 Desert",  // 894
    "4/13 Desert",  // 895
    "4/13 Desert Shadow",  // 896
    "4/17 Desert",  // 897
    "4/15 Desert1",  // 898
    "4/15 Desert1 Shadow",  // 899
    "10/23 Desert",  // 900
    "10/23 Desert Shadow",  // 901
    "10/5 Desert",  // 902
    "10/5 Desert Shadow",  // 903
    "6/13 Desert2",  // 904
    "6/13 Desert2 Shadow",  // 905
    "6/20 Desert",  // 906
    "4/15 Desert2",  // 907
    "4/15 Desert2 Shadow",  // 908
    "8/23 Desert",  // 909
    "8/23 Desert Shadow",  // 910
    "12/1 Desert Overlay",  // 911
    "11/3 Desert",  // 912
    "Unknown913",  // 913
    "Lurker Egg",  // 914
    "Devourer",  // 915
    "Devourer Shadow",  // 916
    "Devourer Birth",  // 917
    "Devourer Death",  // 918
    "Lurker Birth",  // 919
    "Lurker Remnants",  // 920
    "Lurker",  // 921
    "Lurker Shadow",  // 922
    "Overmind Cocoon",  // 923
    "Overmind Cocoon Shadow",  // 924
    "Dark Archon Energy",  // 925
    "Dark Archon Being",  // 926
    "Dark Archon Swirl",  // 927
    "Dark Archon Death",  // 928
    "Corsair",  // 929
    "Corsair Shadow",  // 930
    "Corsair Engines",  // 931
    "Neutron Flare Overlay (Unused)",  // 932
    "Dark Templar (Unit)",  // 933
    "Warp Gate",  // 934
    "Warp Gate Shadow",  // 935
    "Warp Gate Overlay",  // 936
    "Xel'Naga Temple",  // 937
    "Xel'Naga Temple Shadow",  // 938
    "Valkyrie",  // 939
    "Valkyrie Shadow",  // 940
    "Valkyrie Engines",  // 941
    "Valkyrie Engines2 (Unused)",  // 942
    "Valkyrie Afterburners (Unused)",  // 943
    "Medic",  // 944
    "Medic Shadow",  // 945
    "Medic Remnants",  // 946
    "Psi Disrupter",  // 947
    "Psi Disrupter Shadow",  // 948
    "Power Generator",  // 949
    "Power Generator Shadow",  // 950
    "Disruption Web",  // 951
    "Scantid (Desert)",  // 952
    "Scantid (Desert) Shadow",  // 953
    "Kakaru (Twilight)",  // 954
    "Kakaru (Twilight) Shadow",  // 955
    "Ursadon (Ice)",  // 956
    "Ursadon (Ice) Shadow",  // 957
    "Uraj",  // 958
    "Khalis",  // 959
    "Halo Rockets Trail",  // 960
    "Subterranean Spines",  // 961
    "Corrosive Acid Shot",  // 962
    "Corrosive Acid Hit",  // 963
    "Neutron Flare",  // 964
    "Halo Rockets",  // 965
    "Optical Flare Grenade",  // 966
    "Restoration Hit (Small)",  // 967
    "Restoration Hit (Medium)",  // 968
    "Restoration Hit (Large)",  // 969
    "Heal (Small)",  // 970
    "Heal (Medium)",  // 971
    "Heal (Large)",  // 972
    "Mind Control Hit (Small)",  // 973
    "Mind Control Hit (Medium)",  // 974
    "Mind Control Hit (Large)",  // 975
    "Optical Flare Hit (Small)",  // 976
    "Optical Flare Hit (Medium)",  // 977
    "Optical Flare Hit (Large)",  // 978
    "Feedback (Small)",  // 979
    "Feedback (Medium)",  // 980
    "Feedback Hit (Large)",  // 981
    "Maelstrom Overlay (Small)",  // 982
    "Maelstrom Overlay (Medium)",  // 983
    "Maelstrom Overlay (Large)",  // 984
    "Subterranean Spines Hit",  // 985
    "Acid Spores (1) Overlay (Small)",  // 986
    "Acid Spores (2-3) Overlay (Small)",  // 987
    "Acid Spores (4-5) Overlay (Small)",  // 988
    "Acid Spores (6-9) Overlay (Small)",  // 989
    "Acid Spores (1) Overlay (Medium)",  // 990
    "Acid Spores (2-3) Overlay (Medium)",  // 991
    "Acid Spores (4-5) Overlay (Medium)",  // 992
    "Acid Spores (6-9) Overlay (Medium)",  // 993
    "Acid Spores (1) Overlay (Large)",  // 994
    "Acid Spores (2-3) Overlay (Large)",  // 995
    "Acid Spores (4-5) Overlay (Large)",  // 996
    "Acid Spores (6-9) Overlay (Large)",  // 997
    "Maelstrom Hit",  // 998
};
static const char* const kEudUpgradeNames[] = {
    "Terran Infantry Armor",  // 0
    "Terran Vehicle Plating",  // 1
    "Terran Ship Plating",  // 2
    "Zerg Carapace",  // 3
    "Zerg Flyer Carapace",  // 4
    "Protoss Armor",  // 5
    "Protoss Plating",  // 6
    "Terran Infantry Weapons",  // 7
    "Terran Vehicle Weapons",  // 8
    "Terran Ship Weapons",  // 9
    "Zerg Melee Attacks",  // 10
    "Zerg Missile Attacks",  // 11
    "Zerg Flyer Attacks",  // 12
    "Protoss Ground Weapons",  // 13
    "Protoss Air Weapons",  // 14
    "Protoss Plasma Shields",  // 15
    "U-238 Shells",  // 16
    "Ion Thrusters",  // 17
    "Burst Lasers (Unused)",  // 18
    "Titan Reactor (SV +50)",  // 19
    "Ocular Implants",  // 20
    "Moebius Reactor (Ghost +50)",  // 21
    "Apollo Reactor (Wraith +50)",  // 22
    "Colossus Reactor (BC +50)",  // 23
    "Ventral Sacs",  // 24
    "Antennae",  // 25
    "Pneumatized Carapace",  // 26
    "Metabolic Boost",  // 27
    "Adrenal Glands",  // 28
    "Muscular Augments",  // 29
    "Grooved Spines",  // 30
    "Gamete Meiosis (Queen +50)",  // 31
    "Metasynaptic Node (Defiler +50)",  // 32
    "Singularity Charge",  // 33
    "Leg Enhancements",  // 34
    "Scarab Damage",  // 35
    "Reaver Capacity",  // 36
    "Gravitic Drive",  // 37
    "Sensor Array",  // 38
    "Gravitic Boosters",  // 39
    "Khaydarin Amulet (HT +50)",  // 40
    "Apial Sensors",  // 41
    "Gravitic Thrusters",  // 42
    "Carrier Capacity",  // 43
    "Khaydarin Core (Arbiter +50)",  // 44
    nullptr,  // 45
    nullptr,  // 46
    "Argus Jewel (Corsair +50)",  // 47
    nullptr,  // 48
    "Argus Talisman (DA +50)",  // 49
    nullptr,  // 50
    "Caduceus Reactor (Medic +50)",  // 51
    "Chitinous Plating",  // 52
    "Anabolic Synthesis",  // 53
    "Charon Booster",  // 54
    nullptr,  // 55
    nullptr,  // 56
    nullptr,  // 57
    nullptr,  // 58
    nullptr,  // 59
    nullptr,  // 60
};
static const char* const kEudTechNames[] = {
    "Stim Packs",  // 0
    "Lockdown",  // 1
    "EMP Shockwave",  // 2
    "Spider Mines",  // 3
    "Scanner Sweep",  // 4
    "Tank Siege Mode",  // 5
    "Defensive Matrix",  // 6
    "Irradiate",  // 7
    "Yamato Gun",  // 8
    "Cloaking Field",  // 9
    "Personnel Cloaking",  // 10
    "Burrowing",  // 11
    "Infestation",  // 12
    "Spawn Broodlings",  // 13
    "Dark Swarm",  // 14
    "Plague",  // 15
    "Consume",  // 16
    "Ensnare",  // 17
    "Parasite",  // 18
    "Psionic Storm",  // 19
    "Hallucination",  // 20
    "Recall",  // 21
    "Stasis Field",  // 22
    "Archon Warp",  // 23
    "Restoration",  // 24
    "Disruption Web",  // 25
    nullptr,  // 26
    "Mind Control",  // 27
    "Dark Archon Meld",  // 28
    "Feedback",  // 29
    "Optical Flare",  // 30
    "Maelstrom",  // 31
    "Lurker Aspect",  // 32
    nullptr,  // 33
    "Healing",  // 34
    nullptr,  // 35
    nullptr,  // 36
    nullptr,  // 37
    nullptr,  // 38
    nullptr,  // 39
    nullptr,  // 40
    nullptr,  // 41
    nullptr,  // 42
    nullptr,  // 43
};
static const char* const kEudOrderNames[] = {
    "Die",  // 0
    "Stop",  // 1
    "Guard",  // 2
    "Guard (Player)",  // 3
    "Guard (Subunit)",  // 4
    "Guard (Bunker)",  // 5
    "Move",  // 6
    "Reaver Stop",  // 7
    "Attack",  // 8
    "Attack Obscured",  // 9
    "Attack Unit",  // 10
    "Attack In Range",  // 11
    "Attack Ground (Unused)",  // 12
    "Hover (Unused)",  // 13
    "Attack Move",  // 14
    "Building Is Being Infested",  // 15
    "Nothing 1 (Unused)",  // 16
    "Power-Up (Unused)",  // 17
    "Guard (Building)",  // 18
    "Attack (Building)",  // 19
    "Script (Spider Mine)",  // 20
    "Stay In Range",  // 21
    "Attack (Subunit)",  // 22
    "Nothing 2 (Normal)",  // 23
    "Nothing 3 (Unused)",  // 24
    "Drone Start Build",  // 25
    "Drone Build",  // 26
    "Cast Infest",  // 27
    "Cast Infest Obscured",  // 28
    "Start Infest",  // 29
    "Build (SCV)",  // 30
    "Build (Probe)",  // 31
    "Create Building (Probe)",  // 32
    "Is Building (SCV)",  // 33
    "Repair Unit (SCV)",  // 34
    "Repair Unit Obscured (SCV)",  // 35
    "Place Add-On",  // 36
    "Build Add-On",  // 37
    "Train",  // 38
    "Rally to Visible Unit",  // 39
    "Rally to Ground Tile",  // 40
    "Zerg Birth",  // 41
    "Unit Morph",  // 42
    "Building Morph",  // 43
    "Building Is Constructed (Terran)",  // 44
    "Zerg Build Self",  // 45
    "Build (Nydus Canal Exit)",  // 46
    "Enter (Nydus Canal)",  // 47
    "Building Is Constructed (Protoss)",  // 48
    "Follow",  // 49
    "Idle (Carrier)",  // 50
    "Move (Carrier&Reaver)",  // 51
    "Stop (Carrier)",  // 52
    "Attack (Carrier)",  // 53
    "Attack Obscured (Carrier)",  // 54
    "Ignore 2 (Carrier)",  // 55
    "Attack Unit (Carrier)",  // 56
    "Hold Position (Carrier)",  // 57
    "Idle (Reaver)",  // 58
    "Attack (Reaver)",  // 59
    "Attack Obscured (Reaver)",  // 60
    "Attack Unit (Reaver)",  // 61
    "Hold Position (Reaver)",  // 62
    "Train Fighter",  // 63
    "Move&Attack (Interceptor)",  // 64
    "Move&Attack (Scarab)",  // 65
    "Recharge Shields (Unit)",  // 66
    "Recharge Shields (Global)",  // 67
    "Shield Battery Is Recharging",  // 68
    "Return To Parent (Interceptor)",  // 69
    "Land (Drone)",  // 70
    "Land (Building)",  // 71
    "Lift Off (Building)",  // 72
    "Lift Off (Drone)",  // 73
    "Is Lifting Off",  // 74
    "Research Tech",  // 75
    "Research Upgrade",  // 76
    "Idle (Larva)",  // 77
    "Is Spawning Larva",  // 78
    "Move to Harvest",  // 79
    "Move to Harvest Obscured",  // 80
    "Move to Harvest Gas",  // 81
    "Can Enter Gas Mine",  // 82
    "Enter/Exit Gas Mine",  // 83
    "Return Gas",  // 84
    "Move to Harvest Minerals",  // 85
    "Can Harvest Minerals",  // 86
    "Harvesting Minerals",  // 87
    "Harvesting Minerals Interrupted",  // 88
    "Harvest 4 (Unknown)",  // 89
    "Return Minerals",  // 90
    "Harvesting Interrupted - Recharge Shields",  // 91
    "Move/Enter Transport",  // 92
    "Idle (Transport)",  // 93
    "Load Unit (Mobile Transport)",  // 94
    "Load Unit (Bunker)",  // 95
    "Load Unit (Unknown)",  // 96
    "Idle (Power-Up)",  // 97
    "Siege Mode",  // 98
    "Tank Mode",  // 99
    "Watch Target",  // 100
    "Script (Starting Creep Growth)",  // 101
    "Script (Spread Creep&Spawn Larva)",  // 102
    "Script (Stopping Creep Growth)",  // 103
    "Guardian Aspect (Unused)",  // 104
    "Move and Merge Archon",  // 105
    "Completing Archon Summon",  // 106
    "Hold Position",  // 107
    "Hold Position (Queen)",  // 108
    "Cloak",  // 109
    "Decloak",  // 110
    "Unload",  // 111
    "Move and Unload",  // 112
    "Cast Yamato Gun",  // 113
    "Cast Yamato Gun Obscured",  // 114
    "Cast Lockdown",  // 115
    "Burrow",  // 116
    "Idle (Burrowed)",  // 117
    "Unburrow",  // 118
    "Cast Dark Swarm",  // 119
    "Cast Parasite",  // 120
    "Cast Broodling",  // 121
    "Cast EMP Shockwave",  // 122
    "Nuke Wait",  // 123
    "Nuke Train",  // 124
    "Nuke Launch",  // 125
    "Move&Paint Nuke Target",  // 126
    "Attack Unit (Nuke)",  // 127
    "Attack Ground (Nuke)",  // 128
    "Nuke Track (Ghost)",  // 129
    "Init Cloaking Field",  // 130
    "Cloaking Field",  // 131
    "Place Spider Mine",  // 132
    "Right Click Action",  // 133
    "Attack Unit (Infested Terran)",  // 134
    "Attack Ground (Infested Terran)",  // 135
    "Hold Position (Suicide Units)",  // 136
    "Cast Recall",  // 137
    "Recall to Location",  // 138
    "Cast Scanner Sweep",  // 139
    "Idle (Scanner Sweep)",  // 140
    "Cast Defensive Matrix",  // 141
    "Cast Psionic Storm",  // 142
    "Cast Irradiate",  // 143
    "Cast Plague",  // 144
    "Cast Consume",  // 145
    "Cast Ensnare",  // 146
    "Cast Stasis Field",  // 147
    "Cast Hallucination",  // 148
    "Kill Hallucination on Spell",  // 149
    "Reset Collision (2 Units)",  // 150
    "Reset Collision (Harvester&Mine)",  // 151
    "Patrol",  // 152
    "CTF COP (Initialize)",  // 153
    "Idle (CTF COP)",  // 154
    "Unknown CTF COP 2",  // 155
    "Script (Computer)",  // 156
    "Attack Move (Computer)",  // 157
    "Move (Harass)",  // 158
    "Patrol (Computer)",  // 159
    "Guard Post",  // 160
    "Rescuable Passive",  // 161
    "Idle (Neutral)",  // 162
    "Return To Base (Computer)",  // 163
    "Init Psi Provider",  // 164
    "Self Destrucing",  // 165
    "Idle (Critter)",  // 166
    "Hidden Trap",  // 167
    "Open Door",  // 168
    "Close Door",  // 169
    "Hide Trap",  // 170
    "Reveal Trap",  // 171
    "Enable Doodad",  // 172
    "Disable Doodad",  // 173
    "Warp In (Unused)",  // 174
    "Idle (Medic)",  // 175
    "Cast Heal",  // 176
    "Heal Move",  // 177
    "Hold Position&Heal",  // 178
    "Return To Idle After Heal",  // 179
    "Cast Restoration",  // 180
    "Cast Disruption Web",  // 181
    "Cast Mind Control",  // 182
    "Dark Archon Meld",  // 183
    "Cast Feedback",  // 184
    "Cast Optical Flare",  // 185
    "Cast Maelstrom",  // 186
    "Junk Yard Dog",  // 187
    "Fatal",  // 188
};
static const char* const kEudPortraitNames[] = {
    "Marine",  // 0
    "Ghost",  // 1
    "Firebat",  // 2
    "Vulture",  // 3
    "Spider Mine",  // 4
    "Goliath",  // 5
    "Siege Tank",  // 6
    "SCV",  // 7
    "Wraith",  // 8
    "Science Vessel",  // 9
    "Dropship",  // 10
    "Battlecruiser",  // 11
    "Sarah Kerrigan",  // 12
    "Jim Raynor",  // 13
    "Edmund Duke",  // 14
    "Civilian",  // 15
    "Arcturus Mengsk",  // 16
    "Terran Advisor",  // 17
    "Larva",  // 18
    "Egg",  // 19
    "Zergling",  // 20
    "Hydralisk",  // 21
    "Ultralisk",  // 22
    "Broodling",  // 23
    "Drone",  // 24
    "Overlord",  // 25
    "Mutalisk",  // 26
    "Guardian",  // 27
    "Queen",  // 28
    "Defiler",  // 29
    "Scourge",  // 30
    "Cocoon",  // 31
    "Mature Chrysalis",  // 32
    "Infested Terran",  // 33
    "Zasz",  // 34
    "Daggoth",  // 35
    "Infested Kerrigan",  // 36
    "Hunter Killer",  // 37
    "Overmind",  // 38
    "Probe",  // 39
    "Zealot",  // 40
    "Dragoon",  // 41
    "High Templar",  // 42
    "Archon",  // 43
    "Shuttle",  // 44
    "Scout",  // 45
    "Arbiter",  // 46
    "Carrier",  // 47
    "Interceptor",  // 48
    "Dark Templar",  // 49
    "Fenix (Zealot)",  // 50
    "Fenix (Dragoon)",  // 51
    "Zeratul",  // 52
    "Tassadar",  // 53
    "Gantrithor",  // 54
    "Observer",  // 55
    "Reaver",  // 56
    "Scarab",  // 57
    "Khaydarin Crystal Formation",  // 58
    "Aldaris",  // 59
    "Protoss Advisor",  // 60
    "Merc Biker",  // 61
    "Rhynadon",  // 62
    "Bengalaas",  // 63
    "Ragnasaur",  // 64
    "Cargo Ship",  // 65
    "Merc Gunship",  // 66
    "Raider",  // 67
    "Grom Biker",  // 68
    "Space Critter",  // 69
    "Sally",  // 70
    "Greedo",  // 71
    "Boskk",  // 72
    "Peter",  // 73
    "Independent Advisor",  // 74
    "Terran Gas Tank",  // 75
    "Protoss Gas Orb",  // 76
    "Zerg Gas Sac",  // 77
    "Mineral Cluster",  // 78
    "Data Disc",  // 79
    "Psi Emitter",  // 80
    "Khaydarin Crystal",  // 81
    "Flag P1",  // 82
    "Flag P2",  // 83
    "Flag P3",  // 84
    "Flag P4",  // 85
    "Flag P5",  // 86
    "Flag P6",  // 87
    "Flag P7",  // 88
    "Flag P8",  // 89
    "Medic",  // 90
    "Valkyrie",  // 91
    "Dugalle",  // 92
    "Stukov",  // 93
    "Duran",  // 94
    "Artanis",  // 95
    "Raszagal",  // 96
    "Devourer",  // 97
    "Lurker",  // 98
    "Dark Archon",  // 99
    "Corsair",  // 100
    "Scantid",  // 101
    "Kakaru",  // 102
    "Ursadon",  // 103
    "Uraj Crystal",  // 104
    "Khalis Crystal",  // 105
    "Flag P9",  // 106
    "Flag P10",  // 107
    "Flag P11",  // 108
    "Flag P12",  // 109
    "Marine - Talking",  // 110
    "Ghost - Talking",  // 111
    "Firebat - Talking",  // 112
    "Vulture - Talking",  // 113
    "Spider Mine - Talking",  // 114
    "Goliath - Talking",  // 115
    "Siege Tank - Talking",  // 116
    "SCV - Talking",  // 117
    "Wraith - Talking",  // 118
    "Science Vessel - Talking",  // 119
    "Dropship - Talking",  // 120
    "Battlecruiser - Talking",  // 121
    "Sarah Kerrigan - Talking",  // 122
    "Jim Raynor  - Talking",  // 123
    "Edmund Duke - Talking",  // 124
    "Civilian - Talking",  // 125
    "Arcturus Mengsk - Talking",  // 126
    "Terran Advisor - Talking",  // 127
    "Larva - Talking",  // 128
    "Egg - Talking",  // 129
    "Zergling - Talking",  // 130
    "Hydralisk - Talking",  // 131
    "Ultralisk - Talking",  // 132
    "Broodling - Talking",  // 133
    "Drone - Talking",  // 134
    "Overlord - Talking",  // 135
    "Mutalisk - Talking",  // 136
    "Guardian - Talking",  // 137
    "Queen - Talking",  // 138
    "Defiler - Talking",  // 139
    "Scourge - Talking",  // 140
    "Cocoon - Talking",  // 141
    "Mature Chrysalis - Talking",  // 142
    "Infested Terran - Talking",  // 143
    "Zasz - Talking",  // 144
    "Daggoth - Talking",  // 145
    "Infested Kerrigan - Talking",  // 146
    "Hunter Killer - Talking",  // 147
    "Overmind - Talking",  // 148
    "Probe - Talking",  // 149
    "Zealot - Talking",  // 150
    "Dragoon - Talking",  // 151
    "High Templar - Talking",  // 152
    "Archon - Talking",  // 153
    "Shuttle - Talking",  // 154
    "Scout - Talking",  // 155
    "Arbiter - Talking",  // 156
    "Carrier - Talking",  // 157
    "Interceptor - Talking",  // 158
    "Dark Templar - Talking",  // 159
    "Fenix (Zealot) - Talking",  // 160
    "Fenix (Dragoon) - Talking",  // 161
    "Zeratul - Talking",  // 162
    "Tassadar - Talking",  // 163
    "Gantrithor - Talking",  // 164
    "Observer - Talking",  // 165
    "Reaver - Talking",  // 166
    "Scarab - Talking",  // 167
    "Khaydarin Crystal Formation - Talking",  // 168
    "Aldaris - Talking",  // 169
    "Protoss Advisor - Talking",  // 170
    "Merc Biker - Talking",  // 171
    "Rhynadon - Talking",  // 172
    "Bengalaas - Talking",  // 173
    "Ragnasaur - Talking",  // 174
    "Cargo Ship - Talking",  // 175
    "Merc Gunship - Talking",  // 176
    "Raider - Talking",  // 177
    "Grom Biker - Talking",  // 178
    "Space Critter - Talking",  // 179
    "Sally - Talking",  // 180
    "Greedo - Talking",  // 181
    "Boskk - Talking",  // 182
    "Peter - Talking",  // 183
    "Independent Advisor - Talking",  // 184
    "Terran Gas Tank - Talking",  // 185
    "Protoss Gas Orb - Talking",  // 186
    "Zerg Gas Sac - Talking",  // 187
    "Mineral Cluster - Talking",  // 188
    "Data Disc - Talking",  // 189
    "Psi Emitter - Talking",  // 190
    "Khaydarin Crystal - Talking",  // 191
    "Flag P1 - Talking",  // 192
    "Flag P2 - Talking",  // 193
    "Flag P3 - Talking",  // 194
    "Flag P4 - Talking",  // 195
    "Flag P5 - Talking",  // 196
    "Flag P6 - Talking",  // 197
    "Flag P7 - Talking",  // 198
    "Flag P8 - Talking",  // 199
    "Medic - Talking",  // 200
    "Valkyrie - Talking",  // 201
    "Dugalle - Talking",  // 202
    "Stukov - Talking",  // 203
    "Duran - Talking",  // 204
    "Artanis - Talking",  // 205
    "Raszagal - Talking",  // 206
    "Devourer - Talking",  // 207
    "Lurker - Talking",  // 208
    "Dark Archon - Talking",  // 209
    "Corsair - Talking",  // 210
    "Scantid - Talking",  // 211
    "Kakaru - Talking",  // 212
    "Ursadon - Talking",  // 213
    "Uraj Crystal - Talking",  // 214
    "Khalis Crystal - Talking",  // 215
    "Flag P9 - Talking",  // 216
    "Flag P10 - Talking",  // 217
    "Flag P11 - Talking",  // 218
    "Flag P12 - Talking",  // 219
};
