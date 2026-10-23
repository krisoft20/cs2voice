import type { Kill, Round, Voice } from './voicedata'

const MAGIC = 0x54325343
const ROW = 18
const PRESENT = 128

export type NadeKind = 'smoke' | 'flash' | 'he' | 'molotov' | 'decoy'

export type Throw = {
  release: number
  pitch: number
  yaw: number
  x: number
  y: number
  z: number
  fx: number
  fy: number
  fz: number
  jump: number
  keys: number
  click: number
  walked: number
}

export type Nade = {
  id: number
  kind: NadeKind
  thrower: number
  start: number
  det: number
  end: number
  x: number
  y: number
  z: number
  path: Float32Array
  thrown: Throw | null
}

export type Fire = { nade: number; owner: number; start: number; end: number; frames: { at: number; cells: Float32Array }[] }

export type BombKind = 'carry' | 'drop' | 'beginplant' | 'abortplant' | 'planted' | 'defuse' | 'stop' | 'defused' | 'exploded'
export type BombEvent = { at: number; kind: BombKind; slot: number; x: number; y: number; z: number; extra: number }

export type Hurt = { at: number; attacker: number; victim: number; damage: number; armor: number; health: number; hitgroup: number; weapon: string }
export type Shot = { at: number; slot: number; weapon: string }
export type Blind = { at: number; victim: number; attacker: number; seconds: number; nade: number }
export type Death = Kill & { weapon: string; assister: number; flags: number }
export type Stock = { at: number; money: number; items: number[] }

export type TrackRound = Round & { time: number }

export type Lane = {
  slot: number
  steamid: string
  x: Float32Array
  y: Float32Array
  z: Float32Array
  yaw: Float32Array
  hp: Uint8Array
  armor: Uint8Array
  item: Uint16Array
  flags: Uint8Array
  stock: Stock[]
}

export type Track = {
  map: string
  length: number
  dt: number
  times: Float64Array
  lanes: Lane[]
  rounds: TrackRound[]
  kills: Death[]
  hurts: Hurt[]
  shots: Shot[]
  blinds: Blind[]
  nades: Nade[]
  fires: Fire[]
  bomb: BombEvent[]
}

export type Spot = {
  x: number
  y: number
  z: number
  yaw: number
  hp: number
  armor: number
  item: number
  helmet: boolean
  defuser: boolean
  defusing: boolean
  scoped: boolean
  bomb: boolean
  alive: boolean
  ct: boolean
}

type Head = {
  tick_interval: number
  ticks: number
  map: string
  players: [number, string][]
  rounds: [number, number, number, number][]
  kills: [number, number, number, number, string, number, number][]
  hurts: [number, number, number, number, number, number, number, string][]
  shots: [number, number, string][]
  blinds: [number, number, number, number, number][]
  nades: [NadeKind, number, number, number, number, number, number, number, number[], number[]][]
  fires: [number, number, number, number, number[][]][]
  bomb: [number, BombKind, number, number, number, number, number][]
  inventory: [number, number, number, number[]][]
}

export const parseTrack = (buf: ArrayBuffer): Track | null => {
  const view = new DataView(buf)
  if (buf.byteLength < 16 || view.getUint32(0, true) !== MAGIC || view.getUint32(4, true) !== 3) return null
  const headLen = view.getUint32(8, true)
  const head = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 12, headLen))) as Head
  const dt = head.tick_interval
  let at = 12 + headLen
  const count = view.getUint32(at, true)
  at += 4
  const times = new Float64Array(count)
  const lanes = new Map<number, Lane>()
  for (const [slot, steamid] of head.players)
    lanes.set(slot, {
      slot,
      steamid,
      x: new Float32Array(count),
      y: new Float32Array(count),
      z: new Float32Array(count),
      yaw: new Float32Array(count),
      hp: new Uint8Array(count),
      armor: new Uint8Array(count),
      item: new Uint16Array(count),
      flags: new Uint8Array(count),
      stock: [],
    })
  for (let i = 0; i < count; i++) {
    times[i] = view.getInt32(at, true) * dt
    const n = view.getUint8(at + 4)
    at += 5
    for (let k = 0; k < n; k++) {
      const l = lanes.get(view.getUint8(at))
      if (l) {
        l.flags[i] = view.getUint8(at + 1) | PRESENT
        l.hp[i] = view.getUint8(at + 2)
        l.armor[i] = view.getUint8(at + 3)
        l.x[i] = view.getFloat32(at + 4, true)
        l.y[i] = view.getFloat32(at + 8, true)
        l.z[i] = view.getInt16(at + 12, true)
        l.yaw[i] = (view.getUint16(at + 14, true) / 65536) * 360
        l.item[i] = view.getUint16(at + 16, true)
      }
      at += ROW
    }
  }
  for (const [tick, slot, money, items] of head.inventory) lanes.get(slot)?.stock.push({ at: tick * dt, money, items })
  const t = (tick: number) => (tick < 0 ? -1 : tick * dt)
  return {
    map: head.map,
    length: head.ticks * dt,
    dt,
    times,
    lanes: [...lanes.values()],
    rounds: head.rounds.map(([start, live, end, time]) => ({ start: start * dt, live: live < 0 ? start * dt : live * dt, end: end * dt, time })),
    kills: head.kills.map(([tick, killer, victim, h, weapon, assister, flags]) => ({ at: tick * dt, killer, victim, head: !!h, weapon, assister, flags })),
    hurts: head.hurts.map(([tick, attacker, victim, damage, armor, health, hitgroup, weapon]) => ({
      at: tick * dt,
      attacker,
      victim,
      damage,
      armor,
      health,
      hitgroup,
      weapon,
    })),
    shots: head.shots.map(([tick, slot, weapon]) => ({ at: tick * dt, slot, weapon })),
    blinds: head.blinds.map(([tick, victim, attacker, seconds, nade]) => ({ at: tick * dt, victim, attacker, seconds, nade })),
    nades: head.nades.map(([kind, thrower, start, det, end, x, y, z, path, th], id) => {
      const p = new Float32Array(path)
      for (let i = 0; i < p.length; i += 4) p[i] *= dt
      const thrown =
        th && th.length >= 13
          ? { release: th[0] * dt, pitch: th[1], yaw: th[2], x: th[3], y: th[4], z: th[5], fx: th[6], fy: th[7], fz: th[8], jump: th[9], keys: th[10], click: th[11], walked: th[12] * dt }
          : null
      return { id, kind, thrower, start: start * dt, det: t(det), end: t(end), x, y, z, path: p, thrown }
    }),
    fires: head.fires.map(([nade, owner, start, end, frames]) => ({
      nade,
      owner,
      start: start * dt,
      end: t(end),
      frames: frames.map(([tick, ...cells]) => ({ at: tick * dt, cells: new Float32Array(cells) })),
    })),
    bomb: head.bomb.map(([tick, kind, slot, x, y, z, extra]) => ({ at: tick * dt, kind, slot, x, y, z, extra })),
  }
}

export const timelineOf = (track: Track): Voice => ({
  length: track.length,
  speakers: [],
  rounds: track.rounds,
  kills: track.kills,
  spurts: [],
})

const index = (times: Float64Array, t: number) => {
  let lo = 0
  let hi = times.length - 1
  if (hi < 0 || t <= times[0]) return 0
  if (t >= times[hi]) return hi
  while (hi - lo > 1) {
    const mid = (lo + hi) >> 1
    if (times[mid] <= t) lo = mid
    else hi = mid
  }
  return lo
}

const lerpAngle = (a: number, b: number, k: number) => {
  let d = ((b - a + 540) % 360) - 180
  if (d < -180) d += 360
  return a + d * k
}

export const spotAt = (track: Track, lane: Lane, t: number): Spot | null => {
  const i = index(track.times, t)
  if (!(lane.flags[i] & PRESENT)) return null
  const j = Math.min(i + 1, track.times.length - 1)
  const span = track.times[j] - track.times[i]
  const k = span > 0 && lane.flags[j] & PRESENT ? Math.min(Math.max((t - track.times[i]) / span, 0), 1) : 0
  const f = lane.flags[i]
  return {
    x: lane.x[i] + (lane.x[j] - lane.x[i]) * k,
    y: lane.y[i] + (lane.y[j] - lane.y[i]) * k,
    z: lane.z[i] + (lane.z[j] - lane.z[i]) * k,
    yaw: lerpAngle(lane.yaw[i], lane.yaw[j], k),
    hp: lane.hp[i],
    armor: lane.armor[i],
    item: lane.item[i],
    alive: !!(f & 1),
    ct: !!(f & 2),
    bomb: !!(f & 4),
    helmet: !!(f & 8),
    defuser: !!(f & 16),
    defusing: !!(f & 32),
    scoped: !!(f & 64),
  }
}

export const pathOf = (track: Track, lane: Lane, from: number, to: number) => {
  const out: number[] = []
  for (let i = index(track.times, from); i < track.times.length && track.times[i] <= to; i++)
    if (lane.flags[i] & 1) out.push(lane.x[i], lane.y[i])
  return out
}

export const stockAt = (lane: Lane, t: number): Stock | null => {
  let found: Stock | null = null
  for (const s of lane.stock) {
    if (s.at > t) break
    found = s
  }
  return found
}

export const nadeAt = (n: Nade, t: number) => {
  const p = n.path
  if (!p.length || t < p[0]) return null
  let i = 0
  while (i + 4 < p.length && p[i + 4] <= t) i += 4
  if (i + 4 >= p.length) return { x: p[i + 1], y: p[i + 2], z: p[i + 3], i }
  const k = (t - p[i]) / Math.max(p[i + 4] - p[i], 1e-6)
  return { x: p[i + 1] + (p[i + 5] - p[i + 1]) * k, y: p[i + 2] + (p[i + 6] - p[i + 2]) * k, z: p[i + 3] + (p[i + 7] - p[i + 3]) * k, i }
}
