import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { Player as Member, Roster } from './bridge'
import { send } from './bridge'
import { Avatar, remember, remembered, useUi } from './common'
import { Icon, Strip, reasonOf } from './Details'
import { GRENADES, itemIcon, itemName, weaponIcon } from './items'
import type { Playback } from './playback'
import { sideOf as teamSide } from './Summary'
import { nadeAt, pathOf, spotAt, stockAt, type Death, type Nade, type Spot, type Throw, type Track, type TrackRound } from './trackdata'
import { teamLabels } from './voice'
import { clock } from './Voices'

export type RadarInfo = { ok: boolean; pos_x: number; pos_y: number; scale: number; lower: boolean; split_z: number }

const SIZE = 1024
const MARGIN = 0.06
const SMOKE_RADIUS = 144
const FIRE_CELL = 36
const POP = 0.5
const HURT_FADE = 0.7
const KILL_FADE = 1.6
const SHOT_FADE = 0.09
const TRACER = 260
const TRAIL = 6
const LEAD_IN = 2
const ZOOM = 1.18
const MIN_VIEW = 90
const RATES = [0.5, 1, 2, 4]
const FLAG_OPENING = 64
const FLAG_TRADE = 128

type Tab = 'players' | 'duels' | 'utility' | 'log'
type View = { x: number; y: number; size: number }

type Duel = {
  kill: Death
  first: number
  dealt: number
  taken: number
  hits: number
  tags: string[]
}

type Util = { nade: Nade; enemies: number; mates: number; blind: number; damage: number; mateDamage: number }

type Data = {
  kills: Death[]
  hurts: Track['hurts']
  shots: Track['shots']
  blinds: Track['blinds']
  nades: Nade[]
  fires: Track['fires']
  bomb: Track['bomb']
  duels: Duel[]
  utils: Util[]
}

const isGun = (w: string) => !/knife|bayonet|grenade|flashbang|molotov|decoy|inferno|c4|taser|world/.test(w)

export function Radar(props: { roster: Roster; track: Track | null; err?: string; info: RadarInfo | null; pb: Playback; icons: boolean; avatars: Record<string, string> }) {
  const { t, me } = useUi()
  const { roster, track, err, info, pb, icons, avatars } = props
  const [tab, setTabState] = useState<Tab>(() => {
    const v = remembered('cs2voice.radar.tab', 'players')
    return v === 'duels' || v === 'utility' || v === 'log' ? v : 'players'
  })
  const setTab = (v: Tab) => {
    setTabState(v)
    remember('cs2voice.radar.tab', v)
  }
  const [paths, setPathsState] = useState(() => remembered('cs2voice.radar.paths', '1') === '1')
  const setPaths = (v: boolean) => {
    setPathsState(v)
    remember('cs2voice.radar.paths', v ? '1' : '0')
  }
  const [faces, setFacesState] = useState(() => remembered('cs2voice.radar.faces', '0') === '1')
  const setFaces = (v: boolean) => {
    setFacesState(v)
    remember('cs2voice.radar.faces', v ? '1' : '0')
  }
  const [level, setLevel] = useState<'upper' | 'lower'>('upper')
  const [follow, setFollow] = useState<number | null>(null)
  const [pick, setPick] = useState<number | null>(null)
  const [skipFreeze, setSkipFreezeState] = useState(() => remembered('cs2voice.radar.freeze', '1') === '1')
  const setSkipFreeze = (v: boolean) => {
    setSkipFreezeState(v)
    remember('cs2voice.radar.freeze', v ? '1' : '0')
  }
  const [zoom, setZoom] = useState<View | null>(null)
  const [box, setBox] = useState({ w: 1, h: 1 })
  const stage = useRef<HTMLDivElement | null>(null)
  const drag = useRef<{ x: number; y: number; view: View; moved: boolean } | null>(null)
  const at = pb.at

  useEffect(() => {
    if (!icons) send({ type: 'icons' })
  }, [icons])

  const bySlot = useMemo(() => new Map<number, Member>(roster.players.map(p => [p.slot, p])), [roster])
  const laneOf = useMemo(() => new Map(track ? track.lanes.map(l => [l.slot, l]) : []), [track])

  const fit = useMemo<View>(() => {
    if (!track || !info?.ok) return { x: 0, y: 0, size: SIZE }
    let x0 = Infinity
    let x1 = -Infinity
    let y0 = Infinity
    let y1 = -Infinity
    for (const l of track.lanes)
      for (let i = 0; i < l.x.length; i++) {
        if (!(l.flags[i] & 1)) continue
        const px = (l.x[i] - info.pos_x) / info.scale
        const py = (info.pos_y - l.y[i]) / info.scale
        if (px < x0) x0 = px
        if (px > x1) x1 = px
        if (py < y0) y0 = py
        if (py > y1) y1 = py
      }
    if (!Number.isFinite(x0)) return { x: 0, y: 0, size: SIZE }
    const size = Math.min(SIZE, Math.max(x1 - x0, y1 - y0) * (1 + 2 * MARGIN))
    return { x: Math.min(Math.max((x0 + x1) / 2 - size / 2, 0), SIZE - size), y: Math.min(Math.max((y0 + y1) / 2 - size / 2, 0), SIZE - size), size }
  }, [track, info])

  const roundIndex = track ? track.rounds.findIndex(r => at >= r.start && at < r.end) : -1
  const lastIndex = track && roundIndex < 0 ? track.rounds.findIndex(r => r.start > at) - 1 : roundIndex
  const ri = track ? (lastIndex < -1 ? track.rounds.length - 1 : Math.max(0, lastIndex)) : -1
  const round: TrackRound | null = track && ri >= 0 ? track.rounds[ri] : null
  const dt = roster.tick_interval || track?.dt || 1 / 64
  const infoIndex = useMemo(() => {
    if (!round) return -1
    return (roster.round_info ?? []).findIndex(r => Math.abs(r.start * dt - round.live) < 2)
  }, [roster, round, dt])

  const data = useMemo<Data | null>(() => {
    if (!track || !round) return null
    const inRound = (s: number) => s >= round.start && s <= round.end
    const kills = track.kills.filter(k => inRound(k.at))
    const hurts = track.hurts.filter(h => inRound(h.at))
    const shots = track.shots.filter(s => inRound(s.at))
    const blinds = track.blinds.filter(b => inRound(b.at))
    const nades = track.nades.filter(n => inRound(n.start) || (n.start < round.start && n.end > round.start))
    const fires = track.fires.filter(f => inRound(f.start))
    const bomb = track.bomb.filter(b => inRound(b.at))
    const teamOf = (slot: number) => bySlot.get(slot)?.team ?? -1
    const health = new Map<number, number>()
    const removed = hurts.map(h => {
      const was = health.get(h.victim) ?? 100
      health.set(h.victim, h.health)
      return Math.max(0, was - h.health)
    })
    const flagsOf = new Map<string, number>()
    const info = infoIndex >= 0 ? roster.round_info?.[infoIndex] : undefined
    for (const [tick, , victim, , flags] of info?.kills ?? []) flagsOf.set(`${tick}:${victim}`, flags)
    const duels: Duel[] = kills
      .filter(k => k.killer >= 0 && k.killer !== k.victim)
      .map(k => {
        let first = k.at
        let dealt = 0
        let taken = 0
        let hits = 0
        hurts.forEach((h, i) => {
          if (h.at > k.at) return
          const fwd = h.attacker === k.killer && h.victim === k.victim
          const back = h.attacker === k.victim && h.victim === k.killer
          if (!fwd && !back) return
          if (h.at < first) first = h.at
          if (fwd) {
            dealt += removed[i]
            hits++
          } else taken += removed[i]
        })
        const tags: string[] = []
        const rf = flagsOf.get(`${Math.round(k.at / dt)}:${k.victim}`) ?? 0
        if (rf & FLAG_OPENING) tags.push(t('duelOpening'))
        if (rf & FLAG_TRADE) tags.push(t('duelTrade'))
        if (teamOf(k.killer) === teamOf(k.victim)) tags.push(t('duelTeam'))
        return { kill: k, first, dealt, taken, hits, tags }
      })
    const utils: Util[] = nades
      .filter(n => inRound(n.start))
      .map(n => {
        const mine = teamOf(n.thrower)
        let enemies = 0
        let mates = 0
        let blind = 0
        for (const b of blinds) {
          if (b.nade !== n.id) continue
          if (teamOf(b.victim) === mine) mates++
          else {
            enemies++
            blind += b.seconds
          }
        }
        let damage = 0
        let mateDamage = 0
        const fire = n.kind === 'molotov' ? fires.find(f => f.nade === n.id) : undefined
        hurts.forEach((h, i) => {
          if (h.attacker !== n.thrower) return
          const hit =
            (n.kind === 'he' && h.weapon === 'hegrenade' && Math.abs(h.at - n.det) < dt / 2) ||
            (fire && h.weapon === 'inferno' && h.at >= fire.start && h.at <= (fire.end < 0 ? round.end : fire.end))
          if (!hit) return
          if (teamOf(h.victim) === mine) mateDamage += removed[i]
          else damage += removed[i]
        })
        return { nade: n, enemies, mates, blind, damage, mateDamage }
      })
    return { kills, hurts, shots, blinds, nades, fires, bomb, duels, utils }
  }, [track, round, bySlot, roster, infoIndex, dt, t])

  const fitView = zoom ?? fit
  const spots = useMemo(() => {
    const out: { slot: number; spot: Spot; member?: Member }[] = []
    if (!track) return out
    for (const lane of track.lanes) {
      const spot = spotAt(track, lane, at)
      if (spot) out.push({ slot: lane.slot, spot, member: bySlot.get(lane.slot) })
    }
    return out
  }, [track, at, bySlot])

  const followed = follow !== null ? spots.find(s => s.slot === follow && s.spot.alive) : undefined
  const view: View = useMemo(() => {
    if (!followed || !info) return fitView
    const size = zoom ? zoom.size : fit.size * 0.5
    const cx = (followed.spot.x - info.pos_x) / info.scale
    const cy = (info.pos_y - followed.spot.y) / info.scale
    return { x: cx - size / 2, y: cy - size / 2, size }
  }, [followed, info, fitView, zoom, fit])

  useEffect(() => {
    const el = stage.current
    if (!el) return
    const ro = new ResizeObserver(() => setBox({ w: el.clientWidth || 1, h: el.clientHeight || 1 }))
    ro.observe(el)
    return () => ro.disconnect()
  }, [track, info])

  const viewRef = useRef(view)
  viewRef.current = view
  const boxRef = useRef(box)
  boxRef.current = box

  const onWheel = useCallback((e: WheelEvent) => {
    e.preventDefault()
    const v = viewRef.current
    const k = e.deltaY > 0 ? ZOOM : 1 / ZOOM
    const size = Math.min(SIZE, Math.max(MIN_VIEW, v.size * k))
    const el = stage.current
    if (!el) return
    const r = el.getBoundingClientRect()
    const side = Math.min(r.width, r.height)
    const fx = (e.clientX - (r.left + (r.width - side) / 2)) / side
    const fy = (e.clientY - (r.top + (r.height - side) / 2)) / side
    const px = v.x + fx * v.size
    const py = v.y + fy * v.size
    setZoom({ x: px - fx * size, y: py - fy * size, size })
  }, [])

  const stageRef = useCallback(
    (el: HTMLDivElement | null) => {
      stage.current?.removeEventListener('wheel', onWheel)
      stage.current = el
      el?.addEventListener('wheel', onWheel, { passive: false })
    },
    [onWheel],
  )

  const onDown = (e: React.MouseEvent) => {
    if (e.button !== 0) return
    drag.current = { x: e.clientX, y: e.clientY, view: viewRef.current, moved: false }
    const move = (m: MouseEvent) => {
      const d = drag.current
      if (!d) return
      const side = Math.min(boxRef.current.w, boxRef.current.h)
      const dx = ((m.clientX - d.x) / side) * d.view.size
      const dy = ((m.clientY - d.y) / side) * d.view.size
      if (!d.moved && Math.hypot(m.clientX - d.x, m.clientY - d.y) < 4) return
      d.moved = true
      setFollow(null)
      setZoom({ x: d.view.x - dx, y: d.view.y - dy, size: d.view.size })
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      setTimeout(() => (drag.current = null))
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const seekRound = (i: number) => {
    if (!track) return
    const r = track.rounds[Math.max(0, Math.min(track.rounds.length - 1, i))]
    pb.seek(skipFreeze ? r.live : r.start)
  }

  useEffect(() => {
    if (!skipFreeze || !pb.playing || !round || round.live <= round.start) return
    if (at >= round.start && at < round.live) pb.seek(round.live)
  }, [skipFreeze, pb, round, at])

  const pbRef = useRef(pb)
  pbRef.current = pb
  useEffect(
    () => () => {
      if (pbRef.current.playing) pbRef.current.togglePlay()
    },
    [],
  )

  const keys = useRef<(e: KeyboardEvent) => void>(() => {})
  keys.current = (e: KeyboardEvent) => {
    if (!track || (e.target instanceof HTMLElement && /INPUT|TEXTAREA|SELECT/.test(e.target.tagName))) return
    if (e.key === ' ') pb.togglePlay()
    else if (e.key === 'ArrowLeft') pb.seek(Math.max(0, at - 5))
    else if (e.key === 'ArrowRight') pb.seek(Math.min(track.length, at + 5))
    else if (e.key === 'PageUp' || e.key === '[') seekRound(ri - 1)
    else if (e.key === 'PageDown' || e.key === ']') seekRound(ri + 1)
    else if (e.key === 'Escape') {
      setFollow(null)
      setZoom(null)
    } else return
    e.preventDefault()
  }
  useEffect(() => {
    const fn = (e: KeyboardEvent) => keys.current(e)
    window.addEventListener('keydown', fn)
    return () => window.removeEventListener('keydown', fn)
  }, [])

  if (err) return <div className="notice bad">{t('radarFailed', { err })}</div>
  if (!track || !info) return <div className="notice">{t('radarLoading')}</div>
  if (!info.ok) return <div className="notice">{t('radarNoMap')}</div>

  const px = (x: number) => (x - info.pos_x) / info.scale
  const py = (y: number) => (info.pos_y - y) / info.scale
  const unit = view.size / Math.max(1, Math.min(box.w, box.h))
  const onLevel = (z: number) => !info.lower || (level === 'lower' ? z < info.split_z : z >= info.split_z)
  const dimOf = (z: number) => (onLevel(z) ? 1 : 0.28)
  const nameOf = (slot: number) => bySlot.get(slot)?.name ?? t('world')
  const sideAt = (slot: number) => {
    const s = spots.find(p => p.slot === slot)
    if (s) return s.spot.ct ? 'ct' : 't'
    const team = bySlot.get(slot)?.team
    return team !== undefined && infoIndex >= 0 ? (teamSide(roster, team, infoIndex) === 3 ? 'ct' : 't') : ''
  }
  const labels = teamLabels(roster, me, t)
  const timeline = pb.timeline
  const talking = new Set<string>()
  if (timeline) for (const s of timeline.spurts) if (s.start <= at && s.end > at) talking.add(timeline.speakers[s.speaker])

  const live = round ? round.live : 0
  const plant = data?.bomb.find(b => b.kind === 'planted' && b.at <= at)
  const bombOver = data?.bomb.find(b => (b.kind === 'defused' || b.kind === 'exploded') && b.at <= at)
  const roundClock = () => {
    if (!round) return clock(at)
    if (at < live) return clock(live - at)
    if (plant && !bombOver) return clock(plant.at + plant.extra - at)
    return round.time > 0 ? clock(round.time - (at - live)) : clock(at - live)
  }

  const deadAt = new Map<number, { x: number; y: number; z: number }>()
  for (const k of data?.kills ?? []) {
    if (k.at > at) continue
    const lane = laneOf.get(k.victim)
    const s = lane ? spotAt(track, lane, k.at - track.dt) : null
    if (s) deadAt.set(k.victim, { x: s.x, y: s.y, z: s.z })
  }

  const blindOf = (slot: number) => {
    let left = 0
    for (const b of data?.blinds ?? []) if (b.victim === slot && b.at <= at && b.at + b.seconds > at) left = Math.max(left, (b.at + b.seconds - at) / Math.max(b.seconds, 0.01))
    return left
  }

  const map = (
    <svg viewBox={`${view.x} ${view.y} ${view.size} ${view.size}`} className={drag.current?.moved ? 'grabbing' : ''}>
      <image href={`https://cs2voice-maps.example/${track.map}${level === 'lower' ? '_lower' : ''}_radar.png`} x={0} y={0} width={SIZE} height={SIZE} />
      {paths &&
        round &&
        spots.map(({ slot, spot }) => {
          const lane = laneOf.get(slot)
          if (!lane || !spot.alive) return null
          const pts = pathOf(track, lane, Math.max(round.live, at - TRAIL), at)
          if (pts.length < 4) return null
          let d = ''
          for (let i = 0; i < pts.length; i += 2) d += `${i ? 'L' : 'M'}${px(pts[i]).toFixed(1)} ${py(pts[i + 1]).toFixed(1)}`
          return <path key={`p${slot}`} className={`rr-trail ${spot.ct ? 'ct' : 't'}`} d={d} strokeWidth={2.4 * unit} />
        })}
      {data?.fires.map((f, i) => {
        if (at < f.start || (f.end >= 0 && at >= f.end)) return null
        let frame = null
        for (const fr of f.frames) if (fr.at <= at) frame = fr
        if (!frame || !frame.cells.length) return null
        const circles = []
        for (let c = 0; c < frame.cells.length; c += 2)
          circles.push(<circle key={c} cx={px(frame.cells[c])} cy={py(frame.cells[c + 1])} r={FIRE_CELL / info.scale} />)
        return (
          <g key={`f${i}`} className="rr-fire rr-hit" onClick={() => !drag.current?.moved && f.nade >= 0 && (setPick(f.nade), setTab('utility'))}>
            {circles}
          </g>
        )
      })}
      {(() => {
        const n = pick !== null ? data?.nades.find(x => x.id === pick) : undefined
        if (!n) return null
        const th = n.thrown
        let d = ''
        for (let i = 0; i < n.path.length; i += 4) d += `${i ? 'L' : 'M'}${px(n.path[i + 1]).toFixed(1)} ${py(n.path[i + 2]).toFixed(1)}`
        const a = th ? (-th.yaw * Math.PI) / 180 : 0
        return (
          <g className={`rr-pick ${sideAt(n.thrower)}`}>
            {d && <path className="flight" d={d} strokeWidth={1.8 * unit} />}
            {th && th.walked > 0 && (
              <line className="walk" x1={px(th.fx)} y1={py(th.fy)} x2={px(th.x)} y2={py(th.y)} strokeWidth={1.8 * unit} strokeDasharray={`${3 * unit} ${3 * unit}`} />
            )}
            {th && (
              <>
                <line className="aim" x1={px(th.x)} y1={py(th.y)} x2={px(th.x) + Math.cos(a) * 26 * unit} y2={py(th.y) + Math.sin(a) * 26 * unit} strokeWidth={2 * unit} />
                <circle className="spot" cx={px(th.x)} cy={py(th.y)} r={7 * unit} strokeWidth={2.2 * unit} />
              </>
            )}
            <circle className="land" cx={px(n.x)} cy={py(n.y)} r={4.5 * unit} strokeWidth={2 * unit} />
          </g>
        )
      })()}
      {data?.nades.map(n => {
        const thrower = sideAt(n.thrower)
        if (at < n.start) return null
        if (n.det < 0 || at < n.det) {
          const now = nadeAt(n, at)
          if (!now) return null
          let d = ''
          for (let i = 0; i <= now.i; i += 4) d += `${i ? 'L' : 'M'}${px(n.path[i + 1]).toFixed(1)} ${py(n.path[i + 2]).toFixed(1)}`
          d += `L${px(now.x).toFixed(1)} ${py(now.y).toFixed(1)}`
          return (
            <g key={`n${n.id}`} opacity={dimOf(now.z)}>
              <path className={`rr-arc ${thrower}`} d={d} strokeWidth={1.6 * unit} strokeDasharray={`${3 * unit} ${4 * unit}`} />
              <circle className={`rr-nade ${n.kind}`} cx={px(now.x)} cy={py(now.y)} r={4.5 * unit} strokeWidth={1.5 * unit} />
            </g>
          )
        }
        const x = px(n.x)
        const y = py(n.y)
        if (n.kind === 'smoke' && at < n.end) {
          const r = SMOKE_RADIUS / info.scale
          const left = Math.max(0, (n.end - at) / Math.max(n.end - n.det, 0.01))
          const c = 2 * Math.PI * (r + 3 * unit)
          return (
            <g key={`n${n.id}`} opacity={dimOf(n.z)} className="rr-hit" onClick={() => !drag.current?.moved && (setPick(n.id), setTab('utility'))}>
              <circle className={`rr-smoke ${thrower}`} cx={x} cy={y} r={r} />
              <circle
                className="rr-smoke-left"
                cx={x}
                cy={y}
                r={r + 3 * unit}
                strokeWidth={2.5 * unit}
                strokeDasharray={`${c * left} ${c}`}
                transform={`rotate(-90 ${x} ${y})`}
              />
            </g>
          )
        }
        if ((n.kind === 'flash' || n.kind === 'he') && at < n.det + POP) {
          const f = (at - n.det) / POP
          return <circle key={`n${n.id}`} className={`rr-pop ${n.kind}`} cx={x} cy={y} r={(6 + f * 34) * unit} opacity={(1 - f) * dimOf(n.z)} strokeWidth={2.5 * unit} />
        }
        if (n.kind === 'decoy' && at < n.end) return <circle key={`n${n.id}`} className="rr-nade decoy" cx={x} cy={y} r={4 * unit} strokeWidth={1.5 * unit} />
        return null
      })}
      {data?.shots.map((s, i) => {
        if (s.at > at || at - s.at > SHOT_FADE || !isGun(s.weapon)) return null
        const lane = laneOf.get(s.slot)
        const sp = lane ? spotAt(track, lane, s.at) : null
        if (!sp) return null
        const a = (-sp.yaw * Math.PI) / 180
        const len = TRACER / info.scale
        return (
          <line
            key={`s${i}`}
            className="rr-shot"
            x1={px(sp.x)}
            y1={py(sp.y)}
            x2={px(sp.x) + Math.cos(a) * len}
            y2={py(sp.y) + Math.sin(a) * len}
            strokeWidth={1.4 * unit}
            opacity={1 - (at - s.at) / SHOT_FADE}
          />
        )
      })}
      {data?.hurts.map((h, i) => {
        if (h.at > at || at - h.at > HURT_FADE || h.attacker < 0 || h.attacker === h.victim) return null
        const la = laneOf.get(h.attacker)
        const lv = laneOf.get(h.victim)
        const a = la ? spotAt(track, la, h.at) : null
        const v = lv ? spotAt(track, lv, h.at) : null
        if (!a || !v) return null
        const f = 1 - (at - h.at) / HURT_FADE
        return (
          <g key={`h${i}`} opacity={f}>
            <line className="rr-hurt" x1={px(a.x)} y1={py(a.y)} x2={px(v.x)} y2={py(v.y)} strokeWidth={1.6 * unit} />
            <text className="rr-dmg" x={px(v.x) + 14 * unit} y={py(v.y) - 12 * unit} fontSize={15 * unit} strokeWidth={3 * unit}>
              −{h.damage}
              {h.hitgroup === 1 ? ` ${t('radarHs')}` : ''}
            </text>
          </g>
        )
      })}
      {data?.kills.map((k, i) => {
        if (k.at > at || at - k.at > KILL_FADE || k.killer < 0 || k.killer === k.victim) return null
        const la = laneOf.get(k.killer)
        const a = la ? spotAt(track, la, k.at) : null
        const v = deadAt.get(k.victim)
        if (!a || !v) return null
        return <line key={`k${i}`} className="rr-killline" x1={px(a.x)} y1={py(a.y)} x2={px(v.x)} y2={py(v.y)} strokeWidth={2 * unit} opacity={1 - (at - k.at) / KILL_FADE} />
      })}
      {[...deadAt.entries()].map(([slot, p]) => {
        const s = 7 * unit
        return (
          <path
            key={`d${slot}`}
            className={`rr-dead ${sideAt(slot)}`}
            d={`M${px(p.x) - s} ${py(p.y) - s}L${px(p.x) + s} ${py(p.y) + s}M${px(p.x) + s} ${py(p.y) - s}L${px(p.x) - s} ${py(p.y) + s}`}
            strokeWidth={3 * unit}
            opacity={0.85 * dimOf(p.z)}
          />
        )
      })}
      {(() => {
        if (!data) return null
        if (plant && !bombOver) {
          const left = Math.max(0, (plant.at + plant.extra - at) / Math.max(plant.extra, 0.01))
          const r = 13 * unit
          const c = 2 * Math.PI * r
          return (
            <g className="rr-bomb planted">
              <circle cx={px(plant.x)} cy={py(plant.y)} r={r} className="ring-bg" strokeWidth={3 * unit} />
              <circle
                cx={px(plant.x)}
                cy={py(plant.y)}
                r={r}
                className="ring"
                strokeWidth={3 * unit}
                strokeDasharray={`${c * left} ${c}`}
                transform={`rotate(-90 ${px(plant.x)} ${py(plant.y)})`}
              />
              <image href="https://cs2voice-maps.example/icons/icons_equipment_c4.svg" x={px(plant.x) - 8 * unit} y={py(plant.y) - 8 * unit} width={16 * unit} height={16 * unit} />
            </g>
          )
        }
        let drop = null
        for (const b of data.bomb) {
          if (b.at > at) break
          if (b.kind === 'drop') drop = b
          else if (b.kind === 'carry' || b.kind === 'planted') drop = null
        }
        if (!drop) return null
        return (
          <image
            className="rr-bomb"
            href="https://cs2voice-maps.example/icons/icons_equipment_c4.svg"
            x={px(drop.x) - 6 * unit}
            y={py(drop.y) - 6 * unit}
            width={12 * unit}
            height={12 * unit}
            opacity={dimOf(drop.z)}
          />
        )
      })()}
      {spots.map(({ slot, spot, member }) => {
        if (!spot.alive) return null
        const x = px(spot.x)
        const y = py(spot.y)
        const side = spot.ct ? 'ct' : 't'
        const a = (-spot.yaw * Math.PI) / 180
        const cone = 34 * unit
        const half = spot.scoped ? 0.1 : 0.45
        const r = (faces ? 9 : 7) * unit
        const blind = blindOf(slot)
        const on = follow === slot
        const speaking = talking.has(member?.steamid ?? '')
        return (
          <g
            key={slot}
            className={`rr-player ${side}${speaking ? ' talking' : ''}${on ? ' on' : ''}`}
            opacity={dimOf(spot.z)}
            onClick={() => !drag.current?.moved && setFollow(on ? null : slot)}
          >
            <path
              className="cone"
              d={`M${x} ${y}L${x + Math.cos(a - half) * cone} ${y + Math.sin(a - half) * cone}A${cone} ${cone} 0 0 1 ${x + Math.cos(a + half) * cone} ${y + Math.sin(a + half) * cone}Z`}
            />
            {faces && avatars[member?.steamid ?? ''] ? (
              <>
                <clipPath id={`rr-face-${slot}`}>
                  <circle cx={x} cy={y} r={r} />
                </clipPath>
                <image href={avatars[member?.steamid ?? '']} x={x - r} y={y - r} width={2 * r} height={2 * r} clipPath={`url(#rr-face-${slot})`} />
                <circle className="ring" cx={x} cy={y} r={r} strokeWidth={2 * unit} />
              </>
            ) : (
              <circle className="body" cx={x} cy={y} r={r} strokeWidth={1.4 * unit} />
            )}
            {blind > 0 && <circle className="blind" cx={x} cy={y} r={r} opacity={0.25 + 0.75 * blind} />}
            {speaking && <circle className="talk" cx={x} cy={y} r={r + 3.5 * unit} strokeWidth={1.3 * unit} />}
            {spot.hp < 100 && (
              <>
                <rect className="hp-bg" x={x - 8 * unit} y={y + r + 3 * unit} width={16 * unit} height={2.4 * unit} />
                <rect className={`hp${spot.hp < 40 ? ' low' : ''}`} x={x - 8 * unit} y={y + r + 3 * unit} width={(16 * unit * spot.hp) / 100} height={2.4 * unit} />
              </>
            )}
            {spot.bomb && (
              <image href="https://cs2voice-maps.example/icons/icons_equipment_c4.svg" x={x + 6 * unit} y={y - 15 * unit} width={10 * unit} height={10 * unit} />
            )}
            {spot.defusing && <circle className="defusing" cx={x} cy={y} r={r + 6 * unit} strokeWidth={1.8 * unit} />}
            <text className="name" x={x} y={y + r + 16 * unit} fontSize={12 * unit} strokeWidth={2.6 * unit}>
              {member?.name ?? ''}
            </text>
          </g>
        )
      })}
    </svg>
  )

  const roundSpan = round ? Math.max(round.end - round.start, 0.001) : 1
  const rx = (s: number) => (round ? Math.min(100, Math.max(0, ((s - round.start) / roundSpan) * 100)) : 0)
  const winner = infoIndex >= 0 ? roster.winners[infoIndex] : 0
  const reason = infoIndex >= 0 ? roster.round_info?.[infoIndex]?.reason ?? 0 : 0

  return (
    <div className="rr">
      <div className="rr-top">
        <Strip roster={roster} labels={labels} pick={infoIndex >= 0 ? infoIndex : null} onPick={r => seekRound(trackRoundOf(track, roster, r, dt))} icons={icons} />
        <div className="rr-tabs" role="tablist">
          {(['players', 'duels', 'utility', 'log'] as Tab[]).map(k => (
            <button key={k} className={tab === k ? 'on' : ''} onClick={() => setTab(k)}>
              {t(k === 'players' ? 'radarPlayers' : k === 'duels' ? 'radarDuels' : k === 'utility' ? 'radarUtility' : 'radarLog')}
            </button>
          ))}
        </div>
      </div>

      <div className="rr-stage" ref={stageRef} onMouseDown={onDown} onDoubleClick={() => (setZoom(null), setFollow(null))}>
        {map}
        <div className="rr-clock">
          <b className={plant && !bombOver ? 'bomb' : ''}>{roundClock()}</b>
          <small>{round ? t('radarRound', { n: ri + 1 }) : ''}</small>
        </div>
        <div className="rr-tools">
          <button className={paths ? 'on' : ''} onClick={() => setPaths(!paths)}>
            {t('radarPaths')}
          </button>
          <button className={faces ? 'on' : ''} onClick={() => setFaces(!faces)}>
            {t('radarFaces')}
          </button>
          <button className={skipFreeze ? 'on' : ''} onClick={() => setSkipFreeze(!skipFreeze)}>
            {t('radarSkipFreeze')}
          </button>
          {info.lower && (
            <button className={level === 'lower' ? 'on' : ''} onClick={() => setLevel(level === 'lower' ? 'upper' : 'lower')}>
              {t('lower')}
            </button>
          )}
          {(zoom || follow !== null) && (
            <button onClick={() => (setZoom(null), setFollow(null))}>{t('radarReset')}</button>
          )}
        </div>
        <ol className="rr-feed">
          {(data?.kills ?? [])
            .filter(k => k.at <= at && at - k.at < 6)
            .slice(-5)
            .map((k, i) => (
              <li key={i} className={at - k.at < 1.5 ? 'fresh' : ''}>
                <span className={sideAt(k.killer)}>{k.killer >= 0 ? nameOf(k.killer) : t('world')}</span>
                <Icon name={weaponIcon(k.weapon)} ready={icons} className="w" />
                {k.head && <Icon name="hud_deathnotice_icon_headshot" ready={icons} className="f" />}
                {k.flags & 1 ? <Icon name="hud_deathnotice_penetrate" ready={icons} className="f" /> : null}
                {k.flags & 2 ? <Icon name="hud_deathnotice_smoke_kill" ready={icons} className="f" /> : null}
                {k.flags & 4 ? <Icon name="hud_deathnotice_blind_kill" ready={icons} className="f" /> : null}
                <span className={sideAt(k.victim)}>{nameOf(k.victim)}</span>
              </li>
            ))}
        </ol>
      </div>

      <aside className="rr-side">
        {tab === 'players' && (
          <Players
            spots={spots}
            track={track}
            at={at}
            data={data}
            follow={follow}
            setFollow={setFollow}
            talking={talking}
            icons={icons}
            avatars={avatars}
            labels={labels}
            blindOf={blindOf}
          />
        )}
        {tab === 'duels' && data && (
          <Duels duels={data.duels} at={at} live={live} nameOf={nameOf} sideAt={sideAt} icons={icons} onPick={d => pb.seek(Math.max(round?.start ?? 0, d.first - LEAD_IN))} />
        )}
        {tab === 'utility' && data && (
          <Utility
            utils={data.utils}
            at={at}
            live={live}
            nameOf={nameOf}
            sideAt={sideAt}
            icons={icons}
            pick={pick}
            onPick={u => {
              setPick(u.nade.id)
              pb.seek(Math.max(round?.start ?? 0, (u.nade.thrown?.release ?? u.nade.start) - 1))
            }}
          />
        )}
        {tab === 'log' && data && round && <Log data={data} at={at} live={live} nameOf={nameOf} sideAt={sideAt} icons={icons} onPick={s => pb.seek(Math.max(round.start, s - 1))} />}
      </aside>

      <div className="rr-bar">
        <button className="vc-play" onClick={pb.togglePlay} aria-label={pb.playing ? t('pause') : t('play')}>
          {pb.playing ? (
            <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
              <path d="M6.5 4.5h4v15h-4zM13.5 4.5h4v15h-4z" fill="currentColor" />
            </svg>
          ) : (
            <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
              <path d="M7 4.5v15l12-7.5z" fill="currentColor" />
            </svg>
          )}
        </button>
        <button className="rr-rate" onClick={() => pb.setRate(RATES[(RATES.indexOf(pb.rate) + 1) % RATES.length] ?? 1)}>
          {pb.rate}×
        </button>
        <div className="vc-when">
          <b>{round ? t('radarRoundAt', { n: ri + 1, t: roundClock() }) : clock(at)}</b>
          <span>
            {infoIndex >= 0 && winner ? `${t(winner === 3 ? 'radarCtWon' : 'radarTWon')} · ${t(reasonOf(reason).label)} ·` : ''}
            {clock(at)} / {clock(track.length)}
          </span>
        </div>
        <div
          className="rr-scrub"
          onMouseDown={e => {
            if (!round) return
            const r = e.currentTarget.getBoundingClientRect()
            const go = (cx: number) => pb.seek(round.start + Math.min(1, Math.max(0, (cx - r.left) / r.width)) * roundSpan)
            go(e.clientX)
            const move = (m: MouseEvent) => go(m.clientX)
            const up = () => {
              window.removeEventListener('mousemove', move)
              window.removeEventListener('mouseup', up)
            }
            window.addEventListener('mousemove', move)
            window.addEventListener('mouseup', up)
          }}
        >
          {data?.duels.map((d, i) => (
            <span key={`d${i}`} className={`duel ${sideAt(d.kill.killer)}`} style={{ left: `${rx(d.first)}%`, width: `${Math.max(0.4, rx(d.kill.at) - rx(d.first))}%` }} />
          ))}
          <span className="bar" />
          <span className="done" style={{ width: `${rx(at)}%` }} />
          {round && <span className="live" style={{ left: `${rx(round.live)}%` }} />}
          {data?.kills.map((k, i) => (
            <span key={`k${i}`} className={`mark ${sideAt(k.killer)}`} style={{ left: `${rx(k.at)}%` }}>
              <Icon name="icons_ui_kill" ready={icons} />
            </span>
          ))}
          {data?.bomb
            .filter(b => b.kind === 'planted' || b.kind === 'defused' || b.kind === 'exploded')
            .map((b, i) => (
              <span key={`b${i}`} className="mark bomb" style={{ left: `${rx(b.at)}%` }}>
                <Icon name={b.kind === 'defused' ? 'icons_ui_defuser' : 'icons_ui_bomb'} ready={icons} />
              </span>
            ))}
          {data?.nades
            .filter(n => round && n.start >= round.start)
            .map(n => (
              <span key={`n${n.id}`} className="tick nade" style={{ left: `${rx(n.start)}%` }} />
            ))}
          <span className="head" style={{ left: `${rx(at)}%` }} />
        </div>
      </div>
    </div>
  )
}

const trackRoundOf = (track: Track | null, roster: Roster, r: number, dt: number) => {
  if (!track) return 0
  const info = roster.round_info?.[r]
  if (!info) return r
  const i = track.rounds.findIndex(x => Math.abs(x.live - info.start * dt) < 2)
  return i >= 0 ? i : r
}

function Players(props: {
  spots: { slot: number; spot: Spot; member?: Member }[]
  track: Track
  at: number
  data: Data | null
  follow: number | null
  setFollow: (s: number | null) => void
  talking: Set<string>
  icons: boolean
  avatars: Record<string, string>
  labels: string[]
  blindOf: (slot: number) => number
}) {
  const { t } = useUi()
  const { spots, track, at, data, follow, setFollow, talking, icons, avatars, labels, blindOf } = props
  const teams = [0, 1]
    .map(team => ({ team, list: spots.filter(s => s.member?.team === team) }))
    .filter(g => g.list.length)
    .sort((a, b) => Number(b.list[0].spot.ct) - Number(a.list[0].spot.ct))
  return (
    <div className="rr-players">
      {teams.map(({ team, list }) => {
        const side = list[0].spot.ct ? 'ct' : 't'
        const stocks = list.map(s => {
          const lane = track.lanes.find(l => l.slot === s.slot)
          return lane ? stockAt(lane, at) : null
        })
        const money = stocks.reduce((n, s) => n + (s?.money ?? 0), 0)
        return (
          <section key={team} className={`rr-team ${side}`}>
            <header>
              <b>{labels[team]}</b>
              <span>
                {list.filter(s => s.spot.alive).length}/{list.length} · ${money.toLocaleString('en')}
              </span>
            </header>
            {list.map(({ slot, spot, member }, k) => {
              const stock = stocks[k]
              const items = stock?.items ?? []
              const nades = items.filter(i => GRENADES.includes(i))
              const kills = data?.kills.filter(k => k.killer === slot && k.at <= at && k.victim !== slot).length ?? 0
              const blind = blindOf(slot)
              return (
                <button
                  key={slot}
                  className={`rr-card${spot.alive ? '' : ' dead'}${follow === slot ? ' on' : ''}${talking.has(member?.steamid ?? '') ? ' talking' : ''}${blind > 0 ? ' blind' : ''}`}
                  onClick={() => setFollow(follow === slot ? null : slot)}
                  style={{ ['--hp' as string]: `${spot.alive ? spot.hp : 0}%` }}
                >
                  <Avatar url={avatars[member?.steamid ?? '']} name={member?.name ?? '?'} />
                  <span className="nm">
                    {member?.name ?? '?'}
                    {kills > 0 && (
                      <span className="k">
                        <Icon name="icons_ui_kill" ready={icons} />
                        {kills}
                      </span>
                    )}
                  </span>
                  <span className="gear">
                    {spot.alive && (
                      <>
                        {spot.bomb && <Icon name="icons_equipment_c4" ready={icons} className="kit" />}
                        {spot.defuser && <Icon name="icons_equipment_defuser" ready={icons} className="kit" />}
                        {spot.armor > 0 && <Icon name={spot.helmet ? 'icons_equipment_armor_helmet' : 'icons_equipment_armor'} ready={icons} className="kit" />}
                        {nades.map((n, i) => (
                          <Icon key={i} name={itemIcon(n)} ready={icons} className="nade" />
                        ))}
                        <Icon name={itemIcon(spot.item)} ready={icons} className="gun" alt={itemName(spot.item)} />
                      </>
                    )}
                  </span>
                  <b className="hp">{spot.alive ? spot.hp : t('dead')}</b>
                  <span className="mo">${stock?.money ?? 0}</span>
                </button>
              )
            })}
          </section>
        )
      })}
    </div>
  )
}

function Duels(props: {
  duels: Duel[]
  at: number
  live: number
  nameOf: (s: number) => string
  sideAt: (s: number) => string
  icons: boolean
  onPick: (d: Duel) => void
}) {
  const { t } = useUi()
  const { duels, at, live, nameOf, sideAt, icons, onPick } = props
  if (!duels.length) return <p className="rr-empty">{t('radarNoDuels')}</p>
  const won = (side: string) => duels.filter(d => sideAt(d.kill.killer) === side).length
  const cur = latest(duels.map(d => d.first), at)
  return (
    <Follow cur={cur}>
      <div className="rr-sec">
        <span>{t('radarDuelCount', { n: duels.length, ct: won('ct'), t: won('t') })}</span>
      </div>
      {duels.map((d, i) => {
        const k = d.kill
        const ttk = k.at - d.first
        const now = at >= d.first && at <= k.at + 0.5
        return (
          <button key={i} className={`rr-duel${now ? ' on' : ''}${k.at > at ? ' later' : ''}${i === cur ? ' cur' : ''}`} onClick={() => onPick(d)}>
            <span className="at">{clock(Math.max(0, k.at - live))}</span>
            <span className="a">
              <b className={sideAt(k.killer)}>{nameOf(k.killer)}</b>
              <small>
                {t(d.hits === 1 ? 'radarHit' : 'radarHits', { n: d.hits })}
                {d.dealt ? ` · ${t('radarDmg', { n: d.dealt })}` : ''}
                {ttk > 0.01 ? ` · ${ttk.toFixed(2)} s` : ''}
                {d.taken ? ` · ${t('radarTook', { n: d.taken })}` : ''}
              </small>
            </span>
            <span className="w">
              <Icon name={weaponIcon(k.weapon)} ready={icons} />
              {k.head && <Icon name="hud_deathnotice_icon_headshot" ready={icons} className="f" />}
              {k.flags & 1 ? <Icon name="hud_deathnotice_penetrate" ready={icons} className="f" /> : null}
              {k.flags & 2 ? <Icon name="hud_deathnotice_smoke_kill" ready={icons} className="f" /> : null}
              {k.flags & 4 ? <Icon name="hud_deathnotice_blind_kill" ready={icons} className="f" /> : null}
              {k.flags & 8 ? <Icon name="hud_deathnotice_noscope" ready={icons} className="f" /> : null}
            </span>
            <span className="b">
              <b className={`lose ${sideAt(k.victim)}`}>{nameOf(k.victim)}</b>
              <small>{d.tags.join(' · ')}</small>
            </span>
          </button>
        )
      })}
    </Follow>
  )
}

const NADE_ICON: Record<string, string> = {
  smoke: 'icons_equipment_smokegrenade',
  flash: 'icons_equipment_flashbang',
  he: 'icons_equipment_hegrenade',
  molotov: 'icons_equipment_molotov',
  decoy: 'icons_equipment_decoy',
}

function Utility(props: {
  utils: Util[]
  at: number
  live: number
  nameOf: (s: number) => string
  sideAt: (s: number) => string
  icons: boolean
  pick: number | null
  onPick: (u: Util) => void
}) {
  const { t } = useUi()
  const { utils, at, live, nameOf, sideAt, icons, pick, onPick } = props
  if (!utils.length) return <p className="rr-empty">{t('radarNoUtility')}</p>
  const effect = (u: Util) => {
    const n = u.nade
    if (n.kind === 'smoke') return n.end > n.det ? t('radarSmokeHeld', { s: (n.end - n.det).toFixed(0) }) : ''
    if (n.kind === 'flash')
      return [u.enemies ? t('radarFlashed', { n: u.enemies, s: u.blind.toFixed(1) }) : t('radarNoOne'), u.mates ? t('radarMates', { n: u.mates }) : '']
        .filter(Boolean)
        .join(' · ')
    if (n.kind === 'he' || n.kind === 'molotov')
      return [t('radarDmg', { n: u.damage }), u.mateDamage ? t('radarMateDamage', { n: u.mateDamage }) : ''].filter(Boolean).join(' · ')
    return ''
  }
  const count = (side: string) => utils.filter(u => sideAt(u.nade.thrower) === side).length
  const cur = latest(utils.map(u => u.nade.start), at)
  return (
    <Follow cur={cur}>
      <div className="rr-sec">
        <span>{t('radarThrown', { n: utils.length, ct: count('ct'), t: count('t') })}</span>
      </div>
      {utils.map((u, i) => (
        <div key={u.nade.id} className={`rr-utilbox${pick === u.nade.id ? ' on' : ''}`}>
          <button className={`rr-util${u.nade.start > at && pick !== u.nade.id ? ' later' : ''}${i === cur ? ' cur' : ''}`} onClick={() => onPick(u)}>
            <span className="at">{clock(Math.max(0, u.nade.start - live))}</span>
            <Icon name={NADE_ICON[u.nade.kind]} ready={icons} className="ic" />
            <b className={sideAt(u.nade.thrower)}>{nameOf(u.nade.thrower)}</b>
            <small>{effect(u)}</small>
          </button>
          {pick === u.nade.id && <ThrowInfo thrown={u.nade.thrown} />}
        </div>
      ))}
    </Follow>
  )
}

function Log(props: { data: Data; at: number; live: number; nameOf: (s: number) => string; sideAt: (s: number) => string; icons: boolean; onPick: (s: number) => void }) {
  const { t } = useUi()
  const { data, at, live, nameOf, sideAt, icons, onPick } = props
  type Row = { at: number; icon: string; text: React.ReactNode }
  const rows: Row[] = []
  for (const k of data.kills)
    rows.push({
      at: k.at,
      icon: weaponIcon(k.weapon),
      text: (
        <>
          <b className={sideAt(k.killer)}>{k.killer >= 0 ? nameOf(k.killer) : t('world')}</b> {t('radarKilled')} <b className={sideAt(k.victim)}>{nameOf(k.victim)}</b>
        </>
      ),
    })
  for (const n of data.nades)
    rows.push({
      at: n.start,
      icon: NADE_ICON[n.kind],
      text: (
        <>
          <b className={sideAt(n.thrower)}>{nameOf(n.thrower)}</b> {t('radarThrew')}
        </>
      ),
    })
  for (const b of data.bomb) {
    const key = b.kind === 'beginplant' ? 'radarPlanting' : b.kind === 'planted' ? 'radarPlanted' : b.kind === 'defuse' ? 'radarDefusing' : b.kind === 'defused' ? 'radarDefused' : b.kind === 'exploded' ? 'radarExploded' : ''
    if (!key) continue
    rows.push({
      at: b.at,
      icon: b.kind === 'defuse' || b.kind === 'defused' ? 'icons_ui_defuser' : 'icons_ui_bomb',
      text: (
        <>
          {b.slot >= 0 && <b className={sideAt(b.slot)}>{nameOf(b.slot)}</b>} {t(key)}
        </>
      ),
    })
  }
  rows.sort((a, b) => a.at - b.at)
  const cur = latest(
    rows.map(r => r.at),
    at,
  )
  return (
    <Follow cur={cur}>
      {rows.map((r, i) => (
        <button key={i} className={`rr-log${r.at > at ? ' later' : ''}${i === cur ? ' cur' : ''}`} onClick={() => onPick(r.at)}>
          <span className="at">{clock(Math.max(0, r.at - live))}</span>
          <Icon name={r.icon} ready={icons} className="ic" />
          <span className="tx">{r.text}</span>
        </button>
      ))}
    </Follow>
  )
}

const technique = (th: Throw, t: ReturnType<typeof useUi>['t']) => {
  const parts: string[] = []
  if (th.jump === 1) parts.push(t('throwBind'))
  else if (th.jump === 2) parts.push(t('throwJump'))
  else if (th.jump === 3) parts.push(t('throwAir'))
  if (th.keys & 16) parts.push(t('throwCrouch'))
  const keys = ['W', 'A', 'S', 'D'].filter((_, i) => th.keys & (1 << i))
  if (keys.length) {
    const held = th.walked > 0 ? t('throwHeld', { k: keys.join('+'), s: th.walked.toFixed(2) }) : keys.join('+')
    parts.push(th.keys & 32 ? t('throwWalking', { k: held }) : t('throwRunning', { k: held }))
  }
  if (!parts.length) parts.push(t('throwStanding'))
  parts.push(th.click === 3 ? t('clickBoth') : th.click === 2 ? t('clickRight') : t('clickLeft'))
  return parts.join(' · ')
}

function ThrowInfo(props: { thrown: Throw | null }) {
  const { t } = useUi()
  const [copied, setCopied] = useState(false)
  const th = props.thrown
  useEffect(() => {
    if (!copied) return
    const timer = setTimeout(() => setCopied(false), 1800)
    return () => clearTimeout(timer)
  }, [copied])
  if (!th) return <p className="rr-throw none">{t('throwUnknown')}</p>
  const cmd = `setpos ${th.x.toFixed(3)} ${th.y.toFixed(3)} ${th.z.toFixed(3)};setang ${th.pitch.toFixed(6)} ${th.yaw.toFixed(6)} 0`
  return (
    <div className="rr-throw">
      <b>{technique(th, t)}</b>
      <code>{cmd}</code>
      <div className="row">
        <button
          className={copied ? 'done' : ''}
          onClick={() => {
            send({ type: 'copy', text: cmd })
            setCopied(true)
          }}
        >
          {copied ? t('throwCopied') : t('throwCopy')}
        </button>
        <small>{t('throwCheats')}</small>
      </div>
    </div>
  )
}

const latest = (times: number[], at: number) => {
  let cur = -1
  times.forEach((s, i) => {
    if (s <= at) cur = i
  })
  return cur
}

function Follow(props: { cur: number; children: React.ReactNode }) {
  const list = useRef<HTMLDivElement | null>(null)
  useEffect(() => {
    list.current?.querySelector('.cur')?.scrollIntoView({ block: 'nearest', behavior: 'smooth' })
  }, [props.cur])
  return (
    <div className="rr-list" ref={list}>
      {props.children}
    </div>
  )
}
