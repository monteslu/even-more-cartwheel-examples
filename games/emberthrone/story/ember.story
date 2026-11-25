# Ember Throne's story (cartwheel/story.h): tools/story.sh compiles it to assets/story/ember.cwstory and voices it into
# assets/story/ember.cwspeech. The game plays the scenes (main.c story_*): the guide's welcome, each warden's intro, the
# finisher and the choice over the beaten warden, the gate, the King, the ending; and asks the pools for barks in the
# fights and for the shrine keepers' lines when you rest.
#
# The wanderer is the thief. The guide sends you for the keys; the wardens guarded a sleeping King who held the
# fire against the frost, and the guide is the frost's voice. The shrines are embers the King keeps lit (you wake there,
# and the foes come back with them); the bosses stand spent because their own fire is running out.

cast wanderer "The Wanderer" voice=af_bella speed=1.0 color=#7ff0e4
cast guide "The Guide" voice=bf_isabella speed=0.92 color=#cfe3ff
cast thorn "The Thornwarden" voice=bm_george speed=0.9 color=#d6f08a
cast tide "The Tidecaller" voice=bf_emma speed=0.95 color=#8ad8ff
cast king "The Ember King" voice=am_onyx speed=0.88 color=#ffb55c
cast keeper "Old Keeper" voice=bm_lewis speed=0.95 color=#ffe0a8
cast keeper2 "Young Keeper" voice=am_adam speed=1.0 color=#ffd0c0

stat mercy = 0
stat deaths = 0
stat deaths_thorn = 0
stat deaths_tide = 0
stat deaths_king = 0
flag spared_thorn spared_tide killed_thorn killed_tide
flag key_sun key_moon gate_open
flag ending_keeper ending_frost
flag duo

# ── the welcome: the first time the game starts ─────────────────────────
scene intro save
  shot push guide wanderer dur=4
  say guide calm "You came. Good. I was afraid the cold had taken the road."
  shot two guide wanderer
  say guide "Three wardens hold the land. Two of them hold keys: the Sun Key in the Amber Canyon, the Moon Key in the Tidemarsh."
  say guide anger:0.5 "Behind the Ember Keep a king hoards the fire that should warm us all. The keys open his gate."
  choice "Answer the guide" timeout=14 default=1
    option "I will bring you the keys."
      say wanderer "I will bring you the keys."
      say guide joy "I knew you would."
    option "Why me?"
      say wanderer "Why me?"
      say guide calm "Because the shrines answer you. Fall, and the ember lights you again at the last one you rested at."
  end
  say guide "Go north, up the stairs. Rest at every shrine you find."
  emit objective 1
  shot release
end

# ── the Thornwarden ──────────────────────────────────────────────────────
scene thorn_intro
  shot push thorn dur=2.6
  say thorn anger "Thief. I smell the guide's ash on you."
  shot hero wanderer
  say thorn anger:0.8 "Turn back, or I grow my thorns through your ribs."
  shot release
end

scene thorn_defeat save
  shot insert thorn part=head
  say thorn sorrow "Enough. Take the key, then."
  shot two thorn wanderer
  say thorn sorrow "He only sleeps. Every wall I built across the old roads kept you from waking him."
  choice "The Thornwarden kneels" timeout=15 default=1
    option "Leave the warden breathing."
      add mercy 1
      set spared_thorn
      shot react thorn wanderer
      say thorn surprise "Mercy, from a thief. Then I will be there when you understand."
    option "Take the key from a cold hand."
      add mercy -1
      set killed_thorn
      shot insert thorn part=head
      say thorn fear "The thorns... go still."
  end
  set key_sun
  shot release
end

pool thorn.phase cooldown=6 priority=2
  line thorn anger "Every road you walk, I walled. Every wall, I bled for."
  line thorn anger "The canyon remembers the last thief. It wears his bones."
end
pool thorn.spent cooldown=8 priority=1
  line thorn sorrow "My fire... runs thin."
  line thorn "Breathe, old thorn. Breathe."
end
pool thorn.winning cooldown=10 priority=1
  line thorn "Kneel. The thorns are kind to the kneeling."
end
pool thorn.losing cooldown=12 priority=1
  line thorn fear "Not the key. Anything but the key."
end

# ── the Tidecaller ───────────────────────────────────────────────────────
scene tide_intro
  shot push tide dur=2.6
  if flag killed_thorn
    say tide anger "You left the Thornwarden in the canyon dust. You will not leave the marsh at all."
  else
    say tide anger "You walked through his thorns and he let you live. The tide is not so soft."
  end
  shot hero wanderer
  shot release
end

scene tide_defeat save
  shot insert tide part=head
  say tide sorrow "Three of us, around one fire. It was never his crown we guarded. It was his sleep."
  shot two tide wanderer
  say tide sorrow "The frost has a voice. It sounds kind. It has sent wanderers before you."
  choice "The Tidecaller sinks to the water" timeout=15 default=1
    option "Let the tide carry her home."
      add mercy 1
      set spared_tide
      shot react tide wanderer
      say tide calm "Then the tide will answer when you call it."
    option "Hold her under."
      add mercy -1
      set killed_tide
      shot insert tide part=head
      say tide fear "Cold... it is so cold..."
  end
  set key_moon
  shot release
end

pool tide.phase cooldown=6 priority=2
  line tide anger "The marsh keeps what the fire forgets."
  line tide anger "I drowned my own village to keep that door shut!"
end
pool tide.spent cooldown=8 priority=1
  line tide sorrow "The water is so heavy tonight."
end
pool tide.winning cooldown=10 priority=1
  line tide "Sink. It is quieter down here."
end
pool tide.losing cooldown=12 priority=1
  line tide fear "Do not give the keys to her. Whatever she promised."
end

# ── the gate, both keys in hand ──────────────────────────────────────────
scene gate save
  shot two guide wanderer
  if stat mercy <= -2
    say guide joy "Both keys, and both wardens silent. You are better at this than the last one."
  elif stat mercy >= 2
    say guide anger:0.4 "You let them live. Sentiment. It will not matter once the gate is open."
  else
    say guide calm "Both keys. Open it, quickly, before the wardens' blood dries on them."
  end
  shot push guide dur=2
  say guide "He sleeps on the throne at the top of the keep. Wake him, and end him."
  shot release
end

# ── the Ember King ───────────────────────────────────────────────────────
scene king_intro save
  shot push king dur=3
  say king calm "Who woke me? Ah. A wanderer, with my wardens' keys, smelling of frost."
  shot hero wanderer
  if flag spared_thorn or flag spared_tide
    say king sorrow "And still my wardens breathe. Perhaps not all of you is hers."
  else
    say king anger "And my wardens' blood on your hands. Then let us end it."
  end
  shot release
end

pool king.phase cooldown=6 priority=2
  line king anger "Every ember you saw, I lit. Every shrine you woke at, I kept warm."
  line king anger "Listen behind you, wanderer. The cold is laughing."
end
pool king.spent cooldown=8 priority=1
  line king sorrow "A hundred winters... and the fire is so small now."
end
pool king.winning cooldown=10 priority=1
  line king "Rest. I will keep you warm, as I kept them."
end
pool king.losing cooldown=12 priority=1
  line king fear "Not yet. She is so close."
end
pool assist.thorn cooldown=1 priority=3
  line thorn "I told you I would be there."
end
pool assist.tide cooldown=1 priority=3
  line tide "The tide answers, wanderer."
end

scene king_defeat save
  shot insert king part=head
  say king sorrow "There. It is done. Now you feel it. The cold."
  shot two king wanderer
  say guide joy "At last. Give me the fire, little thief. You were only ever my hands."
  say king sorrow "She is the frost. She has always been the frost. Sit, and the fire stays. Give it, and it goes out."
  choice "The fire is in your hands" timeout=20 default=1
    option "Take the throne and keep the fire."
      set ending_keeper
      say wanderer "No. I will keep it."
      say guide anger "You will rot on that chair, as he did!"
    option "Give the fire to the guide."
      set ending_frost
      say wanderer "Take it."
      say guide joy "Thank you. Sleep now. Everyone sleeps now."
  end
  emit ending 1
end

# ── the shrines: what the keepers say when you rest ──────────────────────
pool keeper.rest once priority=1
  line keeper "The Tidecaller took you three times. The tide does not tire. Roll through her waves, then strike while she breathes." if stat deaths_tide >= 3
  line keeper2 "The Thornwarden has felled you three times. When she stands steaming, that is your moment." if stat deaths_thorn >= 3
  line keeper "The King has burned you three times. He is old. Outlast him." if stat deaths_king >= 3
  line keeper2 "Two wanderers now. The fire is calling more of you." if flag duo
  line keeper "Both keys. The wardens kept them for a hundred winters. They did not keep them from you." if flag key_sun and flag key_moon
  line keeper2 "You carry the Moon Key. It drips." if flag key_moon and not flag key_sun
  line keeper "You carry the Sun Key. It is warm. Warm with what, I wonder." if flag key_sun and not flag key_moon
  line keeper "The ember gave you back. It gives everything back, whether we want it or not." if stat deaths >= 1
  line keeper "Rest. The ember knows you now. It knows the foes too: they wake with it." if not flag key_sun
end
pool keeper.idle cooldown=40 priority=0
  line keeper "The shrines were lit by someone. Nobody asks who keeps them lit."
  line keeper2 "Look at the old murals in the temple. Three keepers, one fire."
  line keeper "The frost was further north when I was young. Or I was further south."
  line keeper2 "Leave an offering if you like. The wardens always did."
end
pool fall cooldown=2 priority=3
  line guide "Get up. You are not done."
  line guide "Again. The ember is patient. I am less so."
  line guide "Up. The keys will not walk to me."
end

# ── the endings (the game shows them as its last screen) ────────────────
scene ending_keeper
  shot push king wanderer dur=5
  say keeper calm "The wanderer sat, and the ember took them as it took the King."
  if stat mercy >= 1
    say keeper "The wardens who still breathed came to kneel beside the throne."
  end
  say keeper "Spring stayed. The shrines stayed lit. Someone will come to wake the new keeper, one day."
end
scene ending_frost
  shot top king wanderer
  say keeper2 sorrow "The fire went to her hands, and went out."
  say keeper2 sorrow "The shrines darkened one by one. The foes stayed dead. So did everything else."
end
