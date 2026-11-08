# Ember Throne's cast for build_chars.py. Silhouette first: each type differs in bulk, head shape and outline (hood,
# helmet, plate, robe, cape, crown, horns) before colour; the heroes are bright and warm or cool, the foes darker, the
# bosses each in their region's colour. Heights are the figure before the game's per-kind scale (make-characters.mjs
# SPECS scale: the bosses 1.5 to 1.95).
#   outfit, head, hair: CC0 source files in tools/chars/cc0; drop: outfit parts left off
#   height: the crown of the head (metres, before the kind's scale); head_scale, hand_scale: larger heads and hands
#     read better from a third-person camera; thick: flesh pushed out from each bone (bulk); shoulder: each arm moved out
#   recolor: hue moves on the outfit's painted texture; outfit_grade/skin_grade: sat, val, tint over all of it
#   skin_tint: the face's mean colour (sRGB 0..1); hair_color; iris; glow_eyes (emissive colour)
#   wear: generated pieces (dress.py): cuirass, pauldrons, helm (dome, crest, horns, crown, fins), robe, coat, cape,
#     mask, bracers
GREEN = 105  # the ranger outfit's green cloth (hue, degrees)
CREAM = 45   # the peasant shirt's cream linen
STEEL = (0.2, 0.22, 0.26)
BRONZE = (0.62, 0.38, 0.16)
GOLD = (0.95, 0.68, 0.22)
SPECS = [
    # the wayfarer (player one): quick and light, a teal tunic under amber leather, copper hair in buns, one steel
    # pauldron, bracers and tall boots
    dict(name='wayfarer', outfit='Female_Ranger', decimate=0.65, head='Superhero_Female_FullBody', hair=['Hair_Buns'], drop=['Head_Hood'],
         height=1.70, head_scale=1.1, hand_scale=1.08, arm_out=7,
         recolor=[dict(hue=GREEN, width=40, to=178, sat=1.05, val=1.15), dict(hue=28, width=14, minsat=0.3, to=32, sat=1.15, val=1.1)],
         skin_tint=(0.78, 0.55, 0.42), hair_color=(0.62, 0.2, 0.06), iris=(0.18, 0.5, 0.32), wear=[]),
    # the warden (player two): broad, a crimson tunic, steel plate over it (both shoulders, a breastplate), dark hair
    # and a short beard
    dict(name='warden', outfit='Male_Ranger', decimate=0.6, head='Superhero_Male_FullBody', hair=['Hair_SimpleParted', 'Hair_Beard'], drop=['Head_Hood', 'Acc_Pauldron'],
         height=1.80, head_scale=1.08, hand_scale=1.08, arm_out=9, thick={'Chest': 1.08, 'UpperArm': 1.1, 'Spine': 1.05}, shoulder=0.015,
         recolor=[dict(hue=GREEN, width=40, to=352, sat=1.15, val=1.2), dict(hue=28, width=14, minsat=0.3, sat=0.9, val=0.75)],
         skin_tint=(0.55, 0.36, 0.26), hair_color=(0.16, 0.09, 0.05), iris=(0.25, 0.32, 0.55),
         wear=[dict(kind='cuirass', color=STEEL, trim=(0.85, 0.7, 0.35)), dict(kind='pauldrons', color=STEEL, trim=(0.85, 0.7, 0.35), size=1.0, layers=3)]),
    # hollow raiders: hooded, a rust cloth over the face, amber eyes that glow: quick, lean, a cleaver
    dict(name='raider', outfit='Male_Ranger', decimate=0.5, head='Superhero_Male_FullBody', hair=[], drop=['Acc_Pauldron'], no_brows=False,
         height=1.78, head_scale=1.06, hand_scale=1.06, arm_out=8,
         recolor=[dict(hue=GREEN, width=40, to=62, sat=0.8, val=1.05), dict(hue=28, width=14, minsat=0.3, sat=0.85, val=0.9)],
         outfit_grade=dict(val=1.0), skin_tint=(0.5, 0.4, 0.36), iris=(1.0, 0.55, 0.1), glow_eyes=(1.0, 0.55, 0.08),
         wear=[dict(kind='mask', color=(0.45, 0.15, 0.06))]),
    # stone brutes: huge, grey-skinned, a bronze helm with a crest, heavy bronze shoulders and a plate, slow, a maul
    dict(name='brute', outfit='Male_Peasant', head='Superhero_Male_FullBody', hair=[], drop=[], height=1.80, head_scale=1.0, hand_scale=1.15, arm_out=13,
         thick={'Chest': 1.35, 'Spine': 1.3, 'Hips': 1.15, 'UpperArm': 1.45, 'LowerArm': 1.4, 'UpperLeg': 1.25, 'LowerLeg': 1.2, 'Neck': 1.35}, shoulder=0.07,
         recolor=[dict(hue=CREAM, width=25, minsat=0.05, to=285, sat=2.2, val=0.55), dict(hue=25, width=20, minsat=0.25, sat=0.8, val=0.8)],
         skin_tint=(0.48, 0.5, 0.5), skin_grade=dict(sat=0.35), iris=(1.0, 0.25, 0.1), glow_eyes=(1.0, 0.2, 0.08),
         wear=[dict(kind='helm', style='crest', color=BRONZE, trim=(0.78, 0.55, 0.25), size=1.14),
               dict(kind='pauldrons', color=BRONZE, trim=(0.78, 0.55, 0.25), size=1.3, layers=3, spikes=0),
               dict(kind='cuirass', color=BRONZE, trim=(0.78, 0.55, 0.25), ridge=0.02),
               dict(kind='bracers', color=BRONZE, rough=0.4)]),
    # tide lancers: tall and lean, a long open teal coat, a pale mane, a dark cloth mask, cyan eyes, a spear
    dict(name='lancer', outfit='Male_Peasant', head='Superhero_Male_FullBody', hair=['Hair_Long'], drop=[], height=1.84, head_scale=1.05, hand_scale=1.05, arm_out=8,
         thick={'Chest': 0.95, 'UpperArm': 0.95, 'UpperLeg': 0.95}, recolor=[dict(hue=CREAM, width=25, minsat=0.05, to=215, sat=1.8, val=0.45), dict(hue=25, width=20, minsat=0.25, to=210, sat=0.5, val=0.6)],
         skin_tint=(0.62, 0.56, 0.6), skin_grade=dict(sat=0.6), hair_color=(0.7, 0.75, 0.82), iris=(0.2, 0.95, 1.0), glow_eyes=(0.15, 0.9, 1.0),
         wear=[dict(kind='coat', color=(0.05, 0.42, 0.42), trim=(0.55, 0.85, 0.9), trim_metal=1, hem=0.3, open=0.5, flare=0.14, under_arm=0.04),
               dict(kind='mask', color=(0.07, 0.09, 0.14)), dict(kind='pauldrons', color=(0.55, 0.8, 0.85), size=0.85, layers=2, sides='L')]),
    # the Thornwarden: a hulk in moss-green plate, horns and thorns on the helm and the shoulders, eyes like lime lamps
    dict(name='thornwarden', outfit='Male_Peasant', head='Superhero_Male_FullBody', hair=[], drop=[], height=1.80, head_scale=1.0, hand_scale=1.15, arm_out=14,
         thick={'Chest': 1.35, 'Spine': 1.3, 'Hips': 1.2, 'UpperArm': 1.4, 'LowerArm': 1.35, 'UpperLeg': 1.25, 'LowerLeg': 1.2, 'Neck': 1.3}, shoulder=0.075,
         recolor=[dict(hue=CREAM, width=25, minsat=0.05, to=30, sat=1.6, val=0.4), dict(hue=25, width=20, minsat=0.25, to=80, sat=0.7, val=0.6)],
         skin_tint=(0.4, 0.46, 0.34), skin_grade=dict(sat=0.6), iris=(0.7, 1.0, 0.2), glow_eyes=(0.6, 1.0, 0.15),
         wear=[dict(kind='helm', color=(0.15, 0.27, 0.11), trim=(0.45, 0.42, 0.22), size=1.16, horn_len=0.2, horn_color=(0.4, 0.31, 0.19), thorns=7, thorn_color=(0.4, 0.62, 0.18), style='horns'),
               dict(kind='pauldrons', color=(0.15, 0.27, 0.11), trim=(0.45, 0.42, 0.22), size=1.35, layers=3, spikes=3, spike_len=0.12, spike_color=(0.4, 0.62, 0.18)),
               dict(kind='cuirass', color=(0.13, 0.24, 0.1), trim=(0.45, 0.42, 0.22), ridge=0.025),
               dict(kind='bracers', color=(0.15, 0.27, 0.11))]),
    # the Tidecaller: tall, a violet robe to the floor, silver hair, a shell crown of fins, cyan shoulder shells
    dict(name='tidecaller', outfit='Female_Peasant', head='Superhero_Female_FullBody', hair=['Hair_Long'], drop=[], height=1.76, head_scale=1.06, hand_scale=1.05, arm_out=9,
         recolor=[dict(hue=CREAM, width=25, minsat=0.05, to=275, sat=1.8, val=0.5), dict(hue=25, width=20, minsat=0.25, to=260, sat=0.6, val=0.55)],
         skin_tint=(0.66, 0.68, 0.74), skin_grade=dict(sat=0.5), hair_color=(0.58, 0.64, 0.78), iris=(0.75, 0.4, 1.0), glow_eyes=(0.7, 0.35, 1.0),
         wear=[dict(kind='robe', color=(0.2, 0.09, 0.42), trim=(0.4, 0.85, 0.95), trim_metal=1, hem=0.035, flare=0.2, follow=0.5),
               dict(kind='pauldrons', color=(0.35, 0.78, 0.86), trim=(0.85, 0.92, 0.95), size=0.95, layers=2),
               dict(kind='helm', color=(0.35, 0.78, 0.86), trim=(0.3, 0.72, 0.84), size=1.06, fins=5, style='fins', dome=False)]),
    # the Ember King: the biggest, gold and crimson plate, a crowned helm, a crimson cape, eyes like coals
    dict(name='emberking', outfit='Male_Peasant', head='Superhero_Male_FullBody', hair=['Hair_Beard'], drop=[], height=1.80, head_scale=1.0, hand_scale=1.15, arm_out=14,
         thick={'Chest': 1.38, 'Spine': 1.32, 'Hips': 1.2, 'UpperArm': 1.42, 'LowerArm': 1.38, 'UpperLeg': 1.25, 'LowerLeg': 1.2, 'Neck': 1.3}, shoulder=0.08,
         recolor=[dict(hue=CREAM, width=25, minsat=0.05, to=355, sat=2.2, val=0.5), dict(hue=25, width=20, minsat=0.25, to=5, sat=0.9, val=0.55)],
         skin_tint=(0.5, 0.33, 0.27), hair_color=(0.35, 0.08, 0.03), iris=(1.0, 0.45, 0.05), glow_eyes=(1.0, 0.42, 0.05),
         wear=[dict(kind='helm', color=GOLD, trim=(0.85, 0.15, 0.08), size=1.16, crown_len=1.3, style='crown'),
               dict(kind='pauldrons', color=GOLD, trim=(0.72, 0.1, 0.06), size=1.4, layers=3, spikes=2, spike_len=0.1, spike_color=GOLD),
               dict(kind='cuirass', color=(0.75, 0.1, 0.06), trim=GOLD, ridge=0.025),
               dict(kind='cape', color=(0.6, 0.05, 0.04), trim=GOLD, trim_metal=1, hem=0.35, flare=1.0),
               dict(kind='bracers', color=GOLD)]),
]
