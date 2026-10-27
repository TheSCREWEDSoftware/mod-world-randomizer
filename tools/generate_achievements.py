#!/usr/bin/env python3
"""
Builds data/sql/db-world/world_randomizer_06_achievements.sql: the achievement list (from the reference screenshots)
with icons, descriptions and rewards. Edit the lists below and run:  python tools/generate_achievements.py
Criteria and reward numbers are explained in src/WorldRandomizerAchievements.h.
"""
from pathlib import Path

rows=[]   # (id, category, category_name, name, criteria, value, arg, enabled, description, icon, expansion, parent)
VANILLA, TBC, WRATH = 1, 2, 4   # `expansion` is a mask: add them up, 0 = every expansion
# Versions of one achievement: the Vanilla row keeps its id (and is the parent), TBC is id + 2000, Wrath id + 4000.
VERSION_OFFSET = {VANILLA: 0, TBC: 2000, WRATH: 4000}
# TBC and Wrath level slower (Randomizer.Stats.XPRate.* defaults 0.75 / 0.5), so levelling rewards are bigger there:
# a Vanilla reward is multiplied by 1 / rate (XP percent rounded to 5, gold rounded up).
LEVELLING_FACTOR = {VANILLA: 1.0, TBC: 1 / 0.75, WRATH: 1 / 0.5}
def xp_for(pct, exp): return int(round(pct * LEVELLING_FACTOR[exp] / 5) * 5)
def gold_for(gold, exp): return int(-(-gold * LEVELLING_FACTOR[exp] // 1))
rewards=[] # (achievement_id, idx, type, value, arg, arg2, text)
SCOPE={1:'Any character on your account.',2:'Lifetime, every character on your account.',3:'One character, in one life.',4:'One character, in one life: gold held at once.',5:'Any character on your account.',6:'Lifetime, every character on your account, at level 10 or higher.',7:'One character, in one life: different rare creatures.',8:'Any character.',9:'Any character on your account.',10:'Lifetime, every character on your account.',11:'Lifetime, every character on your account.',12:'Any character on your account.',13:'Every other achievement in this category.'}
WEAPON_ICONS={95:'Ability_Defend',44:'INV_Axe_01',43:'INV_Sword_04',54:'INV_Mace_01',172:'INV_Axe_09',55:'INV_Sword_06',160:'INV_Hammer_05',45:'INV_Weapon_Bow_05',226:'INV_Weapon_Crossbow_01',229:'INV_Spear_06',473:'INV_Gauntlets_04',136:'INV_Staff_08',162:'Ability_Warrior_Revenge',176:'INV_ThrowingAxe_01',228:'INV_Wand_01'}
PROF_ICONS={755:'INV_Misc_Gem_01',773:'INV_Inscription_Tradeskill01',171:'Trade_Alchemy',164:'Trade_BlackSmithing',333:'Trade_Engraving',202:'Trade_Engineering',197:'Trade_Tailoring',165:'INV_Misc_ArmorKit_17',186:'Trade_Mining',182:'Spell_Nature_NatureTouchGrow',393:'INV_Misc_Pelt_Wolf_01',356:'Trade_Fishing',129:'Spell_Holy_SealOfSacrifice',185:'INV_Misc_Food_15'}
CAT_ICONS={2:'INV_Misc_Map_01',3:'Achievement_Quests_Completed_08',4:'INV_Misc_Coin_02',5:'Spell_Holy_DivineIntervention',6:'Spell_Holy_Heal',7:'Ability_Rogue_FeignDeath',8:'INV_Misc_Head_Dragon_01',9:'Spell_Shadow_DeathScream',13:'INV_Pick_02',14:'INV_Misc_Gear_01',19:'INV_Misc_Book_11'}
def icon_for(cat, crit, value, arg):
    if cat==1: return f'Achievement_Level_{value}'
    if crit==9 and arg in WEAPON_ICONS: return WEAPON_ICONS[arg]
    if crit==9 and arg in PROF_ICONS: return PROF_ICONS[arg]
    if crit==10 and arg in PROF_ICONS: return PROF_ICONS[arg]
    if crit==13: return 'Achievement_General'
    return CAT_ICONS.get(cat,'INV_Misc_QuestionMark')
def ach(id_, cat, catname, name, crit, value=0, arg=0, rew=(), enabled=1, expansion=0, parent=0, desc=None):
    rows.append((id_, cat, catname, name, crit, value, arg, enabled, desc if desc is not None else SCOPE.get(crit,''),
                 icon_for(cat,crit,value,arg), expansion, parent))
    for i,r in enumerate(rew):
        t,v,a,a2,txt = (list(r)+[0,0,0,''])[:5] if len(r)<5 else r
        rewards.append((id_, i, t, v, a, a2, txt))

XP,CAP,TAL,BTAL,SGOLD,SITEM,GDROP,HREG,MREG,SRATE,PROF,SPELL,SLOT,SPROF,CHOICE,SBONUS,GMULT,LVLSPELL,TITLE=1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
NONE=0
LEVEL,ZONES,QUESTS,GOLD,HP,DEATHS,RARES,HIGHER,SKILL,GATHER,CRAFT,SPELLC,CATEGORY=1,2,3,4,5,6,7,8,9,10,11,12,13

# 1 Levels
# Every expansion has its own version (bigger XP reward where levelling is slower); 70 is TBC / Wrath, 80 Wrath only
for i,l in enumerate(range(10,90,10)):
    for exp in (VANILLA,TBC,WRATH):
        if (l==70 and exp==VANILLA) or (l==80 and exp!=WRATH): continue
        base=100+i+1; xp=xp_for(50,exp)
        ach(base+VERSION_OFFSET[exp],1,'Levels',f'Reach level {l} on any character',LEVEL,l,0,
            [(XP,xp,0,0,f'XP +{xp}%'),(NONE,0,0,0,'4 Zones Corrupted')],expansion=exp,parent=base)
# 2 Zone discovery
for i,n in enumerate([10,20,30]):
    ach(201+i,2,'Zone Discovery',f'Discover {n} different zones',ZONES,n,0,[(CAP,1,0,0,'+1 Max Level Cap')])
# One achievement, three versions: the riding each expansion had
ach(204,2,'Zone Discovery','Discover all zones',ZONES,0,0,[(LVLSPELL,0,33391,60,'Free level 60 mount (Journeyman Riding at 60)'),(TITLE,0,78,0,'Title: the Explorer')],
    expansion=VANILLA,parent=204,desc='Lifetime, every character on your account. Vanilla version: the epic mount at 60.')
ach(205,2,'Zone Discovery','Discover all zones',ZONES,0,0,[(LVLSPELL,0,34090,70,'Free level 70 flying (Expert Riding at 70)'),(TITLE,0,78,0,'Title: the Explorer')],
    expansion=TBC,parent=204,desc='Lifetime, every character on your account. TBC version: flying at 70.')
ach(206,2,'Zone Discovery','Discover all zones',ZONES,0,0,[(LVLSPELL,0,33391,40,'Free level 40 mount (Journeyman Riding at 40)'),(TITLE,0,78,0,'Title: the Explorer')],
    expansion=WRATH,parent=204,desc='Lifetime, every character on your account. Wrath version: the epic mount at 40.')
# 3 Questing
for i,n in enumerate([5,10,25,50,100]):
    ach(301+i,3,'Questing',f'Complete {n} quests in a single run',QUESTS,n,0,[(TAL,0.1,0,0,'+0.1 Talent Point Gain')]+([(TITLE,0,125,0,'Title: Loremaster')] if n==100 else []))
# 4 Gold
GOLD_STEPS={VANILLA:[10,50,250,1000],TBC:[25,100,500,2000],WRATH:[50,250,1000,5000]}
for exp in (VANILLA,TBC,WRATH):
    for i,n in enumerate(GOLD_STEPS[exp]):
        if i<2: g=gold_for(1,exp); rew=[(SGOLD,g,0,0,f'+{g}g Start')]
        else: rew=[(GDROP,50,0,0,'Enemies drop +50% gold')] if i==2 else [(GDROP,100,0,0,'Enemies drop x2 gold')]
        ach(401+i+VERSION_OFFSET[exp],4,'Gold Milestones',f'Get {n} gold in a single run',GOLD,n,0,rew,
            expansion=exp,parent=401+i)
# 5 Feats (needs dungeon / corrupted zone tracking: listed, off)
ach(501,5,'Feats','Complete a run without dying (all dungeons solo)',LEVEL,80,0,[(NONE,0,0,0,'Double Corrupted Zones + Special Drops')],enabled=0)
# 6 HP
HP_STEPS={VANILLA:[500,1000,2000,5000],TBC:[1000,2500,5000,10000],WRATH:[2000,5000,10000,25000]}
for exp in (VANILLA,TBC,WRATH):
    for i,(n,p) in enumerate(zip(HP_STEPS[exp],[20,20,20,50])):
        ach(601+i+VERSION_OFFSET[exp],6,'HP Milestones',f'Reach {n} total HP on any character',HP,n,0,
            [(HREG,p,0,0,f'+{p}% Health Regen')],expansion=exp,parent=601+i)
# 7 Deaths
ach(701,7,'Deaths','Die 1 time (min level 10)',DEATHS,1,0,[(SITEM,0,118,1,'+1 Minor Healing Potion on start')])
ach(702,7,'Deaths','Die 10 times total (min level 10)',DEATHS,10,0,[(SITEM,0,4496,2,'+2 Six-Slot Bags'),(SITEM,0,118,2,'+2 Potions on start')])
ach(703,7,'Deaths','Die 25 times total (min level 10)',DEATHS,25,0,[(SITEM,0,4240,4,'+4 Eight-Slot Bags'),(SITEM,0,118,5,'+5 Potions on start'),(TITLE,0,172,0,'Title: the Patient')])
# 8 Rares
for i,n in enumerate([5,15,30,50,100]):
    ach(801+i,8,'Rare Kills',f'Kill {n} different rare mobs in a single run',RARES,n,0,[(TAL,0.1,0,0,'+0.1 Talent Point Gain')]+([(TITLE,0,81,0,'Title: the Seeker')] if n==100 else []))
# 9 Chaos goals
ach(901,9,'Chaos Goals','Kill an enemy 5+ levels above you',HIGHER,5,0,[(BTAL,1,0,0,'+1 Talent Point')])
ach(902,9,'Chaos Goals','Kill 50 enemies in the Treasure Zone',LEVEL,80,0,[(NONE,0,0,0,'Enemies drop +50% more green items')],enabled=0)
ach(903,9,'Chaos Goals','Loot a blue item from the Treasure Zone',LEVEL,80,0,[(NONE,0,0,0,'Enemies drop +50% more blue items')],enabled=0)
ach(904,9,'Chaos Goals','Kill 50 elite enemies in Corrupted Zones',LEVEL,80,0,[(NONE,0,0,0,'XP +50%')],enabled=0)
# Weapon skills
weapons=[('Axe',44),('Sword',43),('Mace',54),('Two-Handed Axe',172),('Two-Handed Sword',55),('Two-Handed Mace',160),('Bow',45),('Crossbow',226),('Polearm',229),('Fist',473),('Stave',136),('Unarmed',162),('Thrown',176),('Wand',228)]
anyclass={'Axe':'Axes','Sword':'Swords','Mace':'Maces','Two-Handed Axe':'2H Axes','Two-Handed Sword':'2H Swords','Two-Handed Mace':'2H Maces','Bow':'Bows','Crossbow':'Crossbows','Polearm':'Polearms','Fist':'Fist weapons','Stave':'Staves','Thrown':'Thrown weapons','Wand':'Wands'}
ach(1001,10,'Weapon Skills 150','Train Defense to 150',SKILL,150,95,[(SRATE,1,95,0,'Defense Skill Rate +1')])
for i,(n,s) in enumerate(weapons):
    ach(1002+i,10,'Weapon Skills 150',f'Train {n} to 150',SKILL,150,s,[(SRATE,0.2,s,0,'Weapon Skill Rate +0.2')])
ach(1101,11,'Weapon Skills 225 - Any Class Unlock','Train Defense to 225',SKILL,225,95,[(PROF,0,433,0,'Shields usable by any class')])
i=0
for n,s in weapons:
    if n=='Unarmed': continue
    ach(1102+i,11,'Weapon Skills 225 - Any Class Unlock',f'Train {n} to 225',SKILL,225,s,[(PROF,0,s,0,f'{anyclass[n]} usable by any class')]); i+=1
# Max weapon skill of each expansion: 300 / 350 / 400
WEAPON_MAX={VANILLA:300,TBC:350,WRATH:400}
for exp in (VANILLA,TBC,WRATH):
    m=WEAPON_MAX[exp]; o=VERSION_OFFSET[exp]
    ach(1201+o,12,'Weapon Skills - Max',f'Train Defense to {m}',SKILL,m,95,[(SRATE,1,95,0,'Defense Skill Rate x2')],
        expansion=exp,parent=1201)
    for i,(n,s) in enumerate(weapons):
        ach(1202+i+o,12,'Weapon Skills - Max',f'Train {n} to {m}',SKILL,m,s,[(SRATE,0.5,s,0,'Weapon Skill Rate +0.5')],
            expansion=exp,parent=1202+i)
ach(1299,12,'Weapon Skills - Max','Train all weapon skills to the max',CATEGORY,1,12,[(CAP,2,0,0,'+2 Max Level Cap')],
    desc='Every weapon skill in this category: 300 in Vanilla, 350 in TBC, 400 in Wrath.')
# Gathering (lifetime)
ach(1301,13,'Gathering','Mine 100 ore nodes (lifetime)',GATHER,100,186,[(SITEM,0,2770,20,'Start with 20x Copper Ore')])
ach(1302,13,'Gathering','Herb 100 herb nodes (lifetime)',GATHER,100,182,[(SITEM,0,2447,20,'Start with 20x Peacebloom')])
ach(1303,13,'Gathering','Skin 100 enemies (lifetime)',GATHER,100,393,[(SITEM,0,2934,20,'Start with 20x Ruined Leather Scraps')])
# Crafting
ach(1401,14,'Crafting','Craft 50 items total',CRAFT,50,0,[(SBONUS,50,0,0,'+50 Starting Skill (chosen prof)')])
ach(1402,14,'Crafting','Craft 200 items total',CRAFT,200,0,[(SBONUS,50,0,0,'+50 Starting Skill (chosen prof), stackable')])
# Professions
profs=[('Alchemy',171),('Smithing',164),('Enchanting',333),('Engineering',202),('Tailoring',197),('Leatherworking',165),('Mining',186),('Herbalism',182),('Skinning',393),('Fishing',356),('First Aid',129),('Cooking',185)]
for i,(n,s) in enumerate(profs):
    ach(1501+i,15,'Professions 75',f'Reach skill 75 in {n}',SKILL,75,s,[(SRATE,0.02,s,0,'Skill Rate +2%')])
ach(1599,15,'Professions 75','Reach skill 75 in all professions',CATEGORY,1,15,[(SRATE,0.02,0,0,'Skill Rate +2%, every profession')])
for i,(n,s) in enumerate(profs):
    ach(1601+i,16,'Professions 150',f'Reach skill 150 in {n}',SKILL,150,s,[(CHOICE,0,s,0,'Choose as Starting Profession')])
ach(1699,16,'Professions 150','Reach skill 150 in all professions',CATEGORY,1,16,[(CAP,1,0,0,'+1 Max Level Cap')])
p225={'Alchemy':(NONE,0,0,0,'Poisons usable by any class'),'Smithing':(PROF,0,413,0,'Mail armor wearable by any class'),'Leatherworking':(PROF,0,414,0,'Leather armor wearable by any class')}
for i,(n,s) in enumerate(profs):
    ach(1701+i,17,'Professions 225',f'Reach skill 225 in {n}',SKILL,225,s,[p225.get(n,(SLOT,1,0,0,'Extra Profession Slot +1'))])
ach(1799,17,'Professions 225','Reach skill 225 in all professions',CATEGORY,1,17,[(CAP,1,0,0,'+1 Max Level Cap')])
p300={
 'Alchemy':[(SITEM,0,13518,1,'Start with Recipe: Potion of Petrification')],
 'Smithing':[(PROF,0,293,0,'Plate armor wearable by any class')],
 'Enchanting':[(NONE,0,0,0,'Random enemies drop enchanted gear')],
 'Engineering':[(SITEM,0,4385,1,'Start with Green Tinted Goggles')],
 'Tailoring':[(SITEM,0,14046,2,'Start with 2x Runecloth Bags')],
 'Leatherworking':[],
 'Mining':[(GMULT,3,186,0,'Triple ore per attempt')],
 'Herbalism':[(GMULT,3,182,0,'Triple herbs per attempt')],
 'Skinning':[(GMULT,3,393,0,'Triple hides per attempt')],
 'Fishing':[(SITEM,0,19970,1,'Start with Arcanite Fishing Pole'),(SITEM,0,6533,1,'Start with an Aquadynamic Fish Attractor')],
 'First Aid':[(SITEM,0,16085,1,'Start with Artisan First Aid - Heal Thyself'),(HREG,50,0,0,'+50% HP Regen')],
 'Cooking':[(SITEM,0,16073,1,'Start with Artisan Cookbook'),(MREG,50,0,0,'+50% Mana Regen')]}
for i,(n,s) in enumerate(profs):
    ach(1801+i,18,'Professions 300 - Mastery',f'Reach skill 300 in {n}',SKILL,300,s,p300[n]+[(SPROF,0,s,0,'Permanently Unlocked (start with it)')])
ach(1899,18,'Professions 300 - Mastery','Reach skill 300 in all professions',CATEGORY,1,18,[(CAP,2,0,0,'+2 Max Level Cap')])
specs=[('Armorsmith',9788,164),('Weaponsmith',9787,164),('Dragonscale Leatherworking',10656,165),('Elemental Leatherworking',10658,165),('Tribal Leatherworking',10660,165),('Gnomish Engineering',20219,202),('Goblin Engineering',20222,202)]
for i,(n,sp,sk) in enumerate(specs):
    ach(1901+i,19,'Specializations',f'Gain {n} specialization',SPELLC,1,sp,[(SPELL,0,sp,sk,'Permanently Unlocked from Start (once you have the profession)')])
ach(1999,19,'Specializations','Learn all specializations',CATEGORY,1,19,[(CAP,2,0,0,'+2 Max Level Cap')])
# Outland and Northrend skill caps (new categories, no Vanilla version): 375 in TBC and Wrath, 450 in Wrath only.
# Jewelcrafting came with TBC, Inscription with Wrath. Their ids are in the TBC / Wrath ranges (2000+ / 4000+).
late=profs+[('Jewelcrafting',755),('Inscription',773)]
for i,(n,s) in enumerate(late):
    ach(2501+i,20,'Professions 375',f'Reach skill 375 in {n}',SKILL,375,s,[(SRATE,0.05,s,0,'Skill Rate +5%')],
        expansion=WRATH if s==773 else TBC|WRATH)
ach(2599,20,'Professions 375','Reach skill 375 in all professions',CATEGORY,1,20,[(CAP,1,0,0,'+1 Max Level Cap')],
    expansion=TBC|WRATH)
for i,(n,s) in enumerate(late):
    ach(4801+i,21,'Professions 450',f'Reach skill 450 in {n}',SKILL,450,s,[(SRATE,0.05,s,0,'Skill Rate +5%')],
        expansion=WRATH)
ach(4899,21,'Professions 450','Reach skill 450 in all professions',CATEGORY,1,21,[(CAP,2,0,0,'+2 Max Level Cap')],
    expansion=WRATH)

def q(s): return "'"+str(s).replace("'","''")+"'"
out=["-- mod-world-randomizer: achievements and their rewards (account-wide; see WorldRandomizerAchievements.h for",
"-- the criteria / reward numbers). Rows with `enabled` = 0 need features the module doesn't have yet",
"-- (corrupted / treasure zones, flawless dungeon runs); their rewards with type 0 are text only.",
"DELETE FROM `world_randomizer_achievement` WHERE `id` BETWEEN 100 AND 5999;",
"INSERT INTO `world_randomizer_achievement` (`id`, `category`, `category_name`, `name`, `description`, `icon`, `expansion`, `parent_id`, `criteria`, `value`, `arg`, `enabled`) VALUES",
",\n".join(f"({r[0]}, {r[1]}, {q(r[2])}, {q(r[3])}, {q(r[8])}, {q(r[9])}, {r[10]}, {r[11]}, {r[4]}, {r[5]}, {r[6]}, {r[7]})" for r in rows)+";",
"",
"DELETE FROM `world_randomizer_achievement_reward` WHERE `achievement_id` BETWEEN 100 AND 5999;",
"INSERT INTO `world_randomizer_achievement_reward` (`achievement_id`, `idx`, `type`, `value`, `arg`, `arg2`, `text`) VALUES",
",\n".join(f"({r[0]}, {r[1]}, {r[2]}, {r[3]}, {r[4]}, {r[5]}, {q(r[6])})" for r in rewards)+";"]
open(Path(__file__).resolve().parent.parent / 'data/sql/db-world/world_randomizer_06_achievements.sql','w',encoding='utf-8').write("\n".join(out)+"\n")
print(len(rows),'achievements',sum(r[7] for r in rows),'enabled',len(rewards),'rewards', sum(1 for r in rewards if r[2]==0),'text-only')
