# Monster AI

**Inspector name:** Monster AI · **Lua:** `entity.monsterAI` · **Public field type:** `MonsterAI`

A Monster AI is a Doom monster's brain. It notices targets by sight and sound, decides when to
attack, feels pain, turns on whoever hurt it, and dies when its health runs out.

**It never attacks.** It has no weapons. When it decides to attack it fires an event and a script
callback, and whatever you wire up there is the attack. That can be a projectile, a sound or anything
else. It does play your [flipbooks](#flipbooks) for walking, attacking and dying.

It works with a [Pathfinder](Pathfinder.md), which does the walking. With **Drive Pathfinder** ticked
(the default) it sends the Pathfinder after its target and holds it still while idle. Without a
Pathfinder it still thinks, so a turret works too.

## Quick start

1. Give the enemy a [Collider](Collider.md), a [Rigidbody](Rigidbody.md), a Pathfinder, a
   [Flipbook](Flipbook.md) and a Monster AI.
2. Give the player entity the built-in **Player** tag.
3. Set the Monster AI's **Walking**, **Attacking** and **Dying** flipbooks.
4. Handle the attack, with an event or a script:

```lua
function OnMissileReady(target)
    -- spawn the fireball here
end

function OnMeleeReady(target)
    -- damage the target here
end
```

5. When your damage code hurts the monster, tell it how much:

```lua
enemy.monsterAI:Hurt(attacker, 20)
```

## How it thinks

### Idle

While idle it stands still and, every **Look Interval**, does this:

1. **Listens.** If a [noise](#noise) reached its sector, it wakes on the noise's target. With
   **Ambush** ticked it only does so when it can also see that target.
2. **Looks** for the nearest entity with one of its **Target Tags** (the Player tag when empty). It
   sees a target that is within **Sight Range**, inside its **Field Of View** (or closer than **Near
   Range**, which works all around), with a clear line of sight.

### Chasing

Every **Decision Step** it checks these in order, and stops at the first that applies:

1. **Target gone** (destroyed or disabled): picks the nearest target it can see, or goes back to idle.
2. **It just attacked:** only picks a new walking direction, unless **Aggressive**.
3. **Melee:** with the target within **Melee Range** plus the target's radius, and in sight.
4. **Missile:** with the target in sight, on a roll (see below). Unless **Aggressive**, it only tries
   as the Pathfinder finishes holding a direction, as Doom does.
5. **Active:** a small chance of the Active event, Doom's random monster noises.

After deciding to attack it stands still for **Attack Pause**. Set this to match your Attacking
flipbook, which stops when the pause ends.

### The missile roll

This is Doom's `P_CheckMissileRange`. It always passes right after pain, and never passes during
**Reaction Time**, outside **Missile Min Range** and **Missile Max Range**. Otherwise:

```
against = (distance - Distance Offset - 128 if it has no melee) x Distance Scale, at most Distance Cap
it fires if a random number from 0 to 255 is at least `against`
```

So it fires much more often up close. The defaults are Doom's. For a Cyberdemon's constant fire, set
**Distance Scale** to `0.5`.

### Being hurt

The AI can't tell when something damages the entity, because the damage is yours. Call `Hurt`, or
run the Monster AI dispatch's **Hurt** action:

- The damage comes off **Health**. If that reaches 0 it [dies](#dying), and none of the rest happens.
  With no damage given it only reacts.
- **Reaction Time** ends at once.
- It rolls **Pain Chance**. On a pass it fires Pain, stands still for **Pain Pause**, and its next
  missile roll passes.
- With **Infights** ticked it turns on its attacker, waking if idle. After that, for **Target Lock
  Time**, another attacker can't turn it (Doom's `threshold`).

### Dying

When **Health** reaches 0, through `Hurt`, a script or a Set Field, this is what happens:

- It stops thinking, and its Pathfinder is switched off even with **Drive Pathfinder** unticked. A
  flyer with a Rigidbody falls.
- It plays **Dying**, and fires the Died event and `OnDeath`.
- It stays in the level as a corpse, and other monsters stop chasing it. **Its collider stays on**, so
  turn that off or destroy the entity in the Died event if you want the body gone.

Ignored from then on: `Wake`, `Sleep` and `Hurt`. Raising its health doesn't bring it back. Neither
`Hurt` nor death does anything while the component is off or the entity is disabled.

## Flipbooks

These play on the entity's first [Flipbook](Flipbook.md) component, which it needs for them to work.
An empty field leaves the flipbook alone for that state.

| Field | When |
|---|---|
| **Walking** | Plays while its Pathfinder is walking. It holds on its frame while the entity stands still, in idle or pain. |
| **Attacking** | Plays from its start on a melee or missile decision, until **Attack Pause** ends. Afterward: Walking, or with no Walking, whatever was showing before. |
| **Dying** | Plays from its start on death. Use a Once flipbook, so it ends on the corpse frame. |

It only changes the flipbook when the state changes. One a script starts stays on until then.

## Noise

Monsters hear with Doom's rules. A noise spreads from a sector through every portal with room in it,
so a shut door stops it. It crosses one wall marked **Blocks Sound** and stops at the second.

Each sector it reaches remembers the noise's target until a newer noise. A monster that looks later
still hears it, even if it was moved or enabled afterward.

Nothing makes a noise by itself. Make one when the player fires, using `Game.NoiseAlert(target,
emitter?)` or a **Noise Alert** dispatch:

```lua
Game.NoiseAlert(entity) -- this entity made a noise; whoever hears it comes after this entity
```

## In the editor

### Health and flipbooks

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Health** | `health` | `100` | It dies at `0`. |
| **Walking** | `walkFlipbook` | none | `.fpk` path relative to Assets. See [Flipbooks](#flipbooks). |
| **Attacking** | `attackFlipbook` | none | |
| **Dying** | `dieFlipbook` | none | |

### Senses

| Field | Lua | Default | Notes |
|---|---|---|---|
| *(enable box in the header)* | `isActive` | on | Off: doesn't think. |
| **Target Tags** | | none | Who it hunts. Empty: entities with the Player tag. |
| **Field Of View** | `sightFov` | `180` | Degrees, centred on where it faces. |
| **Sight Range** | `sightRange` | `0` | `0` = no limit. |
| **Near Range** | `nearRange` | `64` | Sees all around within this. |
| **Look Interval** | `lookInterval` | `0.286` | Seconds between looks (10/35). |
| **Hearing** | `hearing` | on | Wakes on a noise. |
| **Ambush** | `ambush` | off | A noise only wakes it with the target in sight. |
| **Start Awake** | | off | Chases the nearest target from the first frame, without noticing it. |

### Attacks

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Has Melee** | `hasMelee` | on | |
| **Melee Range** | `meleeRange` | `44` | Plus the target's radius. |
| **Has Missile** | `hasMissile` | on | |
| **Reaction Time** | `reactionTime` | `0.686` | Seconds chasing before its first missile (24/35). |
| **Missile Min Range** | `missileMinRange` | `0` | The Revenant's `196`. `0` = no limit. |
| **Missile Max Range** | `missileMaxRange` | `0` | The Arch-Vile's `896`. `0` = no limit. |
| **Distance Offset** | `missileDistanceOffset` | `64` | See the roll. |
| **Distance Scale** | `missileDistanceScale` | `1` | Lower fires more. |
| **Distance Cap** | `missileDistanceCap` | `200` | At most this many of 256 rolls fail. |
| **Attack Pause** | `attackPause` | `0.6` | Seconds it stands still after deciding to attack. |
| **Decision Step** | `chaseStep` | `0.0857` | Seconds per decision (3/35). |
| **Aggressive** | `aggressive` | off | Doom's `-fast`: attacks while walking, no gap between attacks. |
| **Face Target On Attack** | `faceTargetOnAttack` | on | |

### Pain, infighting and the rest

| Field | Lua | Default | Notes |
|---|---|---|---|
| **Pain Chance %** | `painChance` | `78.4` | The Imp's 200 of 256. |
| **Pain Pause** | `painPause` | `0.171` | Seconds it stands still in pain. |
| **Infights** | `infights` | on | `Hurt` turns it on its attacker. |
| **Target Lock Time** | `targetLockTime` | `8.57` | Doom's `threshold`, 100 chase steps. |
| **Active Chance %** | `activeChance` | `1.17` | Per decision. Doom's 3 of 256. |
| **Drive Pathfinder** | `drivePathfinder` | on | Off leaves the Pathfinder entirely to you. |

An entity can have several Monster AIs, but only the **first** one thinks. A disabled entity's AI
doesn't think.

## Events and dispatches

These only appear on an entity with a Monster AI. The target (the attacker for Pain) is the
instigator, and what the event's tag filter checks.

| Event | When |
|---|---|
| **Target Spotted** | It wakes and starts chasing. Doom's see sound goes here. |
| **Target Lost** | It goes back to idle, with nothing left to chase. No instigator. |
| **Melee Ready** | It decides on a melee attack. |
| **Missile Ready** | It decides on a ranged attack. |
| **Pain** | A `Hurt` passes Pain Chance. |
| **Active** | Now and then while chasing. Doom's active sound goes here. |
| **Died** | Its health reached 0. The attacker is the instigator, if there is one. |

The **Monster AI** dispatch has these actions: Wake On Player, Wake On Instigator, Wake On Entity,
Sleep, and Hurt (the instigator is the attacker, with a **Damage** field). **Noise Alert** works on any object, and its target
is the player, the instigator or the entity it's on. Every number above also has a **Set Field**
entry, and the component has an **Enable / Disable Component** entry.

## Scripting

Every field in the tables with a Lua name can be read and written. Negative numbers become `0`.

| Property | Type | | Description |
|---|---|---|---|
| `isValid` | boolean | read-only | `false` if the entity or its Monster AI is gone. |
| `state` | MonsterAIState | read-only | `MonsterAIState.Idle`, `.Chasing` or `.Dead`. |
| `target` | Entity? | read-only | Who it is chasing. `nil` while idle. |
| `canSeeTarget` | boolean | read-only | It could see its target now: range, field of view and line of sight. |

| Method | Description |
|---|---|
| `Wake(target)` | Starts chasing `target`, or switches to it if already chasing. |
| `Sleep()` | Back to idle, target forgotten. |
| `Hurt(attacker?, damage?)` | Takes `damage` off its health. See [Being hurt](#being-hurt). |

Callbacks, on every enabled script of the entity:
[`OnTargetSpotted(target)`, `OnTargetLost()`, `OnMeleeReady(target)`, `OnMissileReady(target)`,
`OnPain(attacker)`, `OnActive()` and `OnDeath(attacker)`](CallbackFunctions.md#monster-ai-callbacks). A `Wake` from a
script's `Start` reports on the first frame, once the level has fully started.
