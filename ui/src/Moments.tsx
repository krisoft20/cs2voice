import { useEffect, useRef } from 'react'
import { send, type PerfRow, type Player, type Roster } from './bridge'
import { Avatar, useUi } from './common'
import { FLAGS, Icon, roundedClock } from './Details'
import type { Key } from './i18n'
import { CheckIcon, CloseIcon } from './icons'
import { itemIcon } from './items'
import { sideOf } from './Summary'
import type { NadeKind, Track } from './trackdata'

export type Drill =
  | 'kd'
  | 'adr'
  | 'kast'
  | 'wallbangKills'
  | 'smokeKills'
  | 'utilityRating'
  | 'ttd'
  | 'reaction'
  | 'placement'
  | 'firstShot'
  | 'counterStrafe'
  | 'swings'

export const DRILLS: Drill[] = ['kd', 'adr', 'kast', 'wallbangKills', 'smokeKills', 'utilityRating', 'ttd', 'reaction', 'placement', 'firstShot', 'counterStrafe', 'swings']

const LEAD_SECONDS = 3
const LINE_IN = 0
const LINE_DAMAGE = 3
const LINE_KAST = 5
const KAST = ['K', 'A', 'S', 'T']
const HEAD = 1
const HEARD = 2
const KILLED = 4
const NADE_ICON: Record<NadeKind, string> = { smoke: 'smokegrenade', flash: 'flashbang', he: 'hegrenade', molotov: 'molotov', decoy: 'decoy' }

type Moment = {
  round: number
  tick: number
  start: number
  kind: 'kill' | 'death' | 'round' | 'sample' | 'mark' | 'nade'
  other: number | null
  weapon?: string
  icon?: string
  flags?: number
  value?: number
  good?: boolean
  speed?: number
  limit?: number
  damage?: number
  off?: number
  marks?: number
  ticks?: number
  nade?: NadeKind
  enemies?: number
  mates?: number
  blind?: number
}

const UNITS: Partial<Record<Drill, [string, number]>> = { ttd: ['ms', 0], reaction: ['ms', 0], placement: ['°', 1] }

const roundAt = (roster: Roster, tick: number) => {
  const info = roster.round_info ?? []
  let at = -1
  for (let k = 0; k < info.length && info[k].start <= tick; k++) at = k
  return at
}

const nadesOf = (roster: Roster, slot: number, track: Track): Moment[] => {
  const info = roster.round_info ?? []
  const dt = track.dt || roster.tick_interval || 1 / 64
  const teamOf = new Map(roster.players.map(p => [p.slot, p.team]))
  const mine = teamOf.get(slot)
  const removed = new Map<number, number>()
  const health = new Map<number, number>()
  let round = -1
  track.hurts.forEach((h, i) => {
    const r = roundAt(roster, Math.round(h.at / dt))
    if (r !== round) {
      health.clear()
      round = r
    }
    const was = health.get(h.victim) ?? 100
    health.set(h.victim, h.health)
    removed.set(i, Math.max(0, was - h.health))
  })
  const out: Moment[] = []
  for (const n of track.nades) {
    if (n.thrower !== slot) continue
    const tick = Math.round(n.start / dt)
    const r = roundAt(roster, tick)
    if (r < 0) continue
    let enemies = 0
    let mates = 0
    let blind = 0
    for (const b of track.blinds) {
      if (b.nade !== n.id) continue
      if (teamOf.get(b.victim) === mine) mates++
      else {
        enemies++
        blind += b.seconds
      }
    }
    const fire = n.kind === 'molotov' ? track.fires.find(f => f.nade === n.id) : undefined
    let damage = 0
    track.hurts.forEach((h, i) => {
      if (h.attacker !== slot || teamOf.get(h.victim) === mine) return
      const hit =
        (n.kind === 'he' && h.weapon === 'hegrenade' && Math.abs(h.at - n.det) < dt / 2) ||
        (fire && h.weapon === 'inferno' && h.at >= fire.start && (fire.end < 0 || h.at <= fire.end))
      if (hit) damage += removed.get(i) ?? 0
    })
    out.push({ round: r, tick, start: info[r].start, kind: 'nade', other: null, nade: n.kind, enemies, mates, blind, damage })
  }
  return out
}

export const momentsOf = (roster: Roster, slot: number, kind: Drill, perf?: PerfRow, track?: Track | null): Moment[] => {
  const info = roster.round_info ?? []
  const out: Moment[] = []
  if (kind === 'kd' || kind === 'wallbangKills' || kind === 'smokeKills') {
    info.forEach((ri, round) => {
      for (const [tick, killer, victim, , flags, weapon] of ri.kills) {
        const kill = killer === slot && victim !== slot
        const death = victim === slot
        if (kind === 'kd' ? !kill && !death : !kill || !(flags & (kind === 'wallbangKills' ? 2 : 4))) continue
        out.push({ round, tick, start: ri.start, kind: death ? 'death' : 'kill', other: death ? (killer === slot ? -1 : killer) : victim, weapon, flags })
      }
    })
    return out
  }
  if (kind === 'utilityRating') return track ? nadesOf(roster, slot, track) : []
  if (kind === 'adr' || kind === 'kast') {
    const lines = roster.players.find(p => p.slot === slot)?.stats?.lines ?? []
    lines.forEach((l, round) => {
      const ri = info[round]
      if (!ri || !l[LINE_IN]) return
      out.push({ round, tick: ri.start, start: ri.start, kind: 'round', other: null, value: kind === 'adr' ? l[LINE_DAMAGE] : (l[LINE_KAST] ?? 0) })
    })
    return kind === 'adr' ? out.sort((a, b) => a.value! - b.value! || a.round - b.round) : out.sort((a, b) => Number(!!a.value) - Number(!!b.value) || a.round - b.round)
  }
  const place = (tick: number) => {
    const round = roundAt(roster, tick)
    return round < 0 ? null : { round, start: info[round].start }
  }
  if (kind === 'ttd' || kind === 'reaction' || kind === 'placement') {
    for (const [tick, other, value] of perf?.[`${kind}_at`] ?? []) {
      const at = place(tick)
      if (at) out.push({ ...at, tick, kind: 'sample', other, value })
    }
    return out.sort((a, b) => b.value! - a.value!)
  }
  const list = kind === 'firstShot' ? perf?.first_at : kind === 'counterStrafe' ? perf?.stop_at : perf?.swings_at
  for (const [tick, good, item, speed, limit, other, damage, off, marks, ticks] of list ?? []) {
    const at = place(tick)
    if (!at) continue
    out.push({ ...at, tick, kind: 'mark', other: other >= 0 ? other : null, good: !!good, icon: item ? itemIcon(item) : undefined, speed, limit, damage, off, marks, ticks })
  }
  if (kind === 'counterStrafe') return out.sort((a, b) => Number(a.good) - Number(b.good) || b.speed! / b.limit! - a.speed! / a.limit!)
  if (kind === 'firstShot') return out.sort((a, b) => Number(a.good) - Number(b.good) || b.off! - a.off! || a.tick - b.tick)
  return out.sort((a, b) => Number(a.good) - Number(b.good) || a.tick - b.tick)
}

export function Moments(props: {
  roster: Roster
  player: Player
  kind: Drill
  label: Key
  perf?: PerfRow
  track?: Track | null
  avatars: Record<string, string>
  icons: boolean
  copied: string
  radar: boolean
  voices: boolean
  onJump: (to: 'radar' | 'voices', at: number) => void
  onClose: () => void
}) {
  const { t, lang } = useUi()
  const { roster, player, kind, label, perf, track, avatars, icons, copied, radar, voices, onJump, onClose } = props
  const ti = roster.tick_interval || 1 / 64
  const bySlot = new Map(roster.players.map(p => [p.slot, p]))
  const list = momentsOf(roster, player.slot, kind, perf, track)
  const sideCls = (slot: number, round: number) => {
    const p = bySlot.get(slot)
    return p ? (sideOf(roster, p.team, round) === 3 ? 'ct' : 't') : ''
  }
  const lead = (m: Moment) => (m.kind === 'round' ? m.start : Math.max(m.start, m.tick - Math.round(LEAD_SECONDS / ti)))
  const box = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!icons) send({ type: 'icons' })
  }, [icons])
  useEffect(() => {
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches
    box.current?.scrollIntoView({ block: 'nearest', behavior: still ? 'auto' : 'smooth' })
  }, [kind])
  useEffect(() => {
    const key = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [onClose])

  const num = (v: number, digits = 0) => v.toLocaleString(lang, { maximumFractionDigits: digits, minimumFractionDigits: digits })
  const speed = (v: number) => `${num(v)} u/s`

  const what = (m: Moment) => {
    if (m.kind === 'kill' || m.kind === 'death') return t(m.kind === 'death' ? 'momentDeath' : 'momentKill')
    if (m.kind === 'sample') {
      const [unit, digits] = UNITS[kind]!
      return `${num(m.value!, digits)} ${unit}`
    }
    if (m.kind === 'nade') {
      if (m.nade === 'flash') return t('momentBlinded', { n: m.enemies!, s: num(m.blind!, 1) })
      if (m.nade === 'he' || m.nade === 'molotov') return t('radarDmg', { n: m.damage! })
      return ''
    }
    if (m.kind === 'mark') {
      if (kind === 'firstShot') return m.good ? t('radarDmg', { n: m.damage! }) : m.off! >= 0 ? t('momentOff', { n: num(m.off!, 1) }) : '–'
      return speed(m.speed!)
    }
    if (kind === 'adr') return t('radarDmg', { n: m.value! })
    return (
      <span className="mo-kast">
        {KAST.map((c, i) => (
          <b key={c} className={m.value! & (1 << i) ? 'on' : ''}>
            {c}
          </b>
        ))}
      </span>
    )
  }
  const bad = (m: Moment) =>
    m.kind === 'death' ||
    (m.kind === 'mark' && !m.good) ||
    (m.kind === 'nade' && (m.nade === 'flash' ? !m.enemies : (m.nade === 'he' || m.nade === 'molotov') && !m.damage)) ||
    (kind === 'kast' && !m.value) ||
    (kind === 'adr' && m.value === 0)
  const tags = (m: Moment) => {
    const out: { text: string; bad?: boolean }[] = []
    if ((m.flags ?? 0) & 64) out.push({ text: t('tagOpening') })
    if ((m.flags ?? 0) & 128) out.push({ text: t('tagTrade') })
    if (m.kind === 'nade' && m.mates) out.push({ text: t('momentTeam', { n: m.mates }), bad: true })
    if (m.kind === 'mark' && kind === 'counterStrafe') out.push({ text: t('momentLimit', { n: num(m.limit!) }) })
    if (m.kind === 'mark' && kind === 'swings') {
      out.push({ text: `${num(m.ticks! * ti, 2)} s` })
      if (m.marks! & HEARD) out.push({ text: t('momentFootsteps'), bad: true })
      if (m.marks! & KILLED) out.push({ text: t('momentKill') })
    }
    return out
  }

  return (
    <div ref={box} className="moments" role="region" aria-label={t(label)}>
      <div className="mo-head">
        <h3>{t(label)}</h3>
        <span className="mo-count">{list.length}</span>
        <button className="mo-close" onClick={onClose} aria-label={t('close')}>
          <CloseIcon />
        </button>
      </div>
      <ol className="mo-list">
        {list.map(m => {
          const other = m.other === null ? undefined : bySlot.get(m.other)
          const cmd = `demo_gototick ${lead(m)}`
          const icon = m.weapon ? `icons_equipment_${m.weapon}` : m.nade ? `icons_equipment_${NADE_ICON[m.nade]}` : m.icon
          return (
            <li key={`${m.tick}-${m.other}-${m.round}-${m.nade ?? ''}`} className={`mo-row${bad(m) ? ' bad' : ''}`}>
              <span className="mo-round">{t('roundN', { n: m.round + 1 })}</span>
              <span className="mo-time">{m.kind === 'round' ? '' : roundedClock((m.tick - m.start) * ti)}</span>
              <span className="mo-kind">{what(m)}</span>
              <span className="mo-gun">
                {icon && <Icon name={icon} ready={icons} className="mo-weapon" />}
                {FLAGS.filter(([f]) => (m.flags ?? 0) & f).map(([f, n]) => (
                  <Icon key={f} name={n} ready={icons} className="mo-flag" />
                ))}
                {m.kind === 'mark' && (m.marks ?? 0) & HEAD ? <Icon name={FLAGS[0][1]} ready={icons} className="mo-flag" /> : null}
              </span>
              <span className={`mo-other ${m.other !== null && m.other >= 0 ? sideCls(m.other, m.round) : ''}`}>
                {other ? <Avatar url={avatars[other.steamid]} name={other.name} color={other.color} /> : null}
                <span>{other ? other.name || t('slot', { n: other.slot }) : m.other === -1 ? t('world') : ''}</span>
              </span>
              <span className="mo-tags">
                {tags(m).map(g => (
                  <em key={g.text} className={g.bad ? 'bad' : ''}>
                    {g.text}
                  </em>
                ))}
              </span>
              <span className="mo-go">
                <button onClick={() => onJump('radar', lead(m) * ti)} disabled={!radar}>
                  {t('tabRadar')}
                </button>
                <button onClick={() => onJump('voices', lead(m) * ti)} disabled={!voices}>
                  {t('tabVoices')}
                </button>
                <button className={copied === cmd ? 'done' : ''} onClick={() => send({ type: 'copy', text: cmd })}>
                  {copied === cmd ? <CheckIcon /> : null}
                  {copied === cmd ? t('copied') : t('copyTick')}
                </button>
              </span>
            </li>
          )
        })}
      </ol>
    </div>
  )
}
