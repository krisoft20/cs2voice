import { useEffect, useLayoutEffect, useMemo, useRef, useState, type PointerEvent, type ReactNode } from 'react'
import { send, type Clip, type Demo, type Player, type PlayerState, type ReplayEvent, type ReplayMatch, type Roster } from './bridge'
import { Avatar, useUi } from './common'
import type { Key, T } from './i18n'
import { ICONS, Icon, reasonOf } from './Details'
import { AspectIcon, BombIcon, BookmarkIcon, CheckIcon, ChevronDownIcon, CloseIcon, CopyIcon, DemosIcon, DotsIcon, ExpandIcon, MuteIcon, PanelIcon, PauseIcon, PlayIcon, SaveIcon, ScissorsIcon, ShrinkIcon, SkipBackIcon, SkipForwardIcon, SkullIcon, StarIcon, StepBackIcon, StepForwardIcon, TargetIcon, TradeIcon, UsersIcon, VolumeIcon } from './icons'
import { weaponIcon, weaponName } from './items'
import { when } from './voice'

export type ReplaysData = { folder: string; background: number; keep: number; rate?: number; buffer?: number; length?: number; free?: number; saved_dir?: string; matches: ReplayMatch[] }

type Kind = 'kill' | 'assist' | 'death' | 'bomb' | 'mark'

type Moment = {
  t: number
  kind: Kind
  round: number
  killer?: Player
  victim?: Player
  assister?: Player
  weapon?: string
  head?: boolean
  title: string
  text: string
  side?: number
}

type Round = { n: number; t: number; won: number; us?: number; them?: number; reason?: number; side?: number; dmg?: number }

type Pair = { demo: Demo; roster: Roster; offset: number }

type Span = [number, number]
type ChunkRow = [number, number, number, number]
type CutKind = 'freeze' | 'breaks' | 'dead' | 'outside'
type ListTab = 'latest' | 'fav' | 'saved'

type View = { moments: Moment[]; rounds: Round[]; heat: number[]; me?: Player; pair: Pair | null }

const TICK = 1 / 64
const PREROLL = 3
const HL_BEFORE = 5
const HL_AFTER = 3
const DIP_MS = 130
const CURTAIN_MS = 60
const TWEEN_MS = 260
const HEAT_BINS = 240
const CLUSTER_PX = 24
const PEEK_MS = 350
const GOOD_MIN = 5
const CUT_KINDS: CutKind[] = ['freeze', 'breaks', 'dead', 'outside']
const CUT_DEFAULT: CutKind[] = ['freeze', 'breaks', 'outside']
const CUT_LABEL = { freeze: 'replayCutFreeze', breaks: 'replayCutBreaks', dead: 'replayCutDead', outside: 'replayCutOutside' } as const
const SCOREBOARD_TAIL = 8
const CLIP_BEFORE = 10
const NOW_HZ = 4
const FRAME_TEXT_HZ = 10
const CLIP_PAD = 6
const CLIP_MIN = 0.5
const CLIP_AFTER = 5
const STREAK_GAP = 8
const AUDIO_BPS = 210000
const BPP_MIN = 0.07
const BPP_FLOOR = 0.035
const MIN_HEIGHT = 720
const BPP_ORIGINAL = 0.19
const SIZE_SAFETY = 0.93
const PRESETS: { id: string; label: Key; max: number }[] = [
  { id: 'original', label: 'clipOriginal', max: 0 },
  { id: 'discord20', label: 'clipDiscord20', max: 20e6 },
  { id: 'discord50', label: 'clipDiscord50', max: 50e6 },
  { id: 'discord500', label: 'clipDiscord500', max: 500e6 },
]
const GOOD_SHARE = 0.75
const HL_KINDS: Kind[] = ['kill', 'assist', 'death', 'bomb', 'mark']
const HL_DEFAULT: Kind[] = ['kill', 'assist', 'bomb', 'mark']
const HL_LABEL = { kill: 'replayHlKill', assist: 'replayHlAssist', death: 'replayHlDeath', bomb: 'replayHlBomb', mark: 'replayHlMark' } as const
const MIN_RATE = 0.1
const MAX_RATE = 4
const RATE_TICKS = [0.25, 0.5, 1, 2, 4]
const GLYPHS: Record<string, string> = { kill: 'hud_deathnotice_domination', death: 'icons_ui_kill', bomb: 'icons_equipment_c4' }

const mapId = (name: string) => {
  const n = name.toLowerCase().replace(/\s+ii$/, '2').replace(/[^a-z0-9]/g, '')
  return (n === 'office' || n === 'italy' ? 'cs_' : 'de_') + n
}

const stored = (key: string) => {
  try {
    return localStorage.getItem(key)
  } catch {
    return null
  }
}

const store = (key: string, value: string) => {
  try {
    localStorage.setItem(key, value)
  } catch {
    return
  }
}

const clock = (s: number) => {
  const v = Math.max(0, Math.floor(s))
  return `${Math.floor(v / 60)}:${String(v % 60).padStart(2, '0')}`
}

const steamKind = (e: ReplayEvent): Kind | 'round' | null => {
  const [, kind, icon] = e
  if (kind === 'usermarker') return 'mark'
  if (kind !== 'event') return null
  if (/^cs2_\d+$/.test(icon)) return 'round'
  if (icon === 'cs2_death') return 'death'
  if (icon.startsWith('cs2_bomb')) return 'bomb'
  if (icon.endsWith('_kill') && icon !== 'cs2_double_kill' && icon !== 'cs2_multi_kill') return 'kill'
  return null
}

const steamRounds = (m: ReplayMatch) => {
  const out = new Map<number, number>()
  for (const e of m.events) if (steamKind(e) === 'round') out.set(Number(e[2].slice(4)), e[0])
  return out
}

type Mark = { t: number; kind: 'kill' | 'death' }

const PAIR_WINDOW = 0.6
const PAIR_ANCHORS = 12

const closest = (list: Mark[], s: Mark, off: number) => {
  let best: Mark | null = null
  for (const x of list) if (x.kind === s.kind && (!best || Math.abs(x.t + off - s.t) < Math.abs(best.t + off - s.t))) best = x
  return best && Math.abs(best.t + off - s.t) < PAIR_WINDOW ? best : null
}

const pairDemo = (m: ReplayMatch, demos: Demo[], rosters: Record<string, Roster>, me: string): Pair | null => {
  if (m.at === undefined || !me) return null
  const steam: Mark[] = []
  for (const e of m.events) {
    const k = steamKind(e)
    if (k === 'kill' || k === 'death') steam.push({ t: m.at + e[0], kind: k })
  }
  if (steam.length < 3) return null
  const id = mapId(m.map)
  let best: Pair | null = null
  let score = 0
  for (const d of demos) {
    const r = rosters[d.path] ?? d.info
    if (!r || r.map !== id || !r.round_info?.length) continue
    const self = r.players.find(p => p.steamid === me)
    if (!self) continue
    const mine: Mark[] = []
    for (const ri of r.round_info)
      for (const [tick, killer, victim] of ri.kills) {
        if (victim === self.slot) mine.push({ t: tick * TICK, kind: 'death' })
        else if (killer === self.slot) mine.push({ t: tick * TICK, kind: 'kill' })
      }
    let off = 0
    let hits = 0
    for (const s of steam.slice(0, PAIR_ANCHORS))
      for (const x of mine) {
        if (x.kind !== s.kind) continue
        const n = steam.filter(s2 => closest(mine, s2, s.t - x.t)).length
        if (n > hits) {
          hits = n
          off = s.t - x.t
        }
      }
    if (hits < 3 || hits < steam.length * 0.6 || hits <= score) continue
    const diffs = steam.flatMap(s => {
      const x = closest(mine, s, off)
      return x ? [s.t - x.t] : []
    })
    diffs.sort((a, b) => a - b)
    score = hits
    best = { demo: d, roster: r, offset: diffs[diffs.length >> 1] }
  }
  return best
}

const heatOf = (times: number[], from: number, to: number) => {
  const span = Math.max(1, to - from)
  const sigma = Math.max(4, span / 160)
  const out = new Array<number>(HEAT_BINS).fill(0)
  for (const t of times)
    for (let i = 0; i < HEAT_BINS; i++) {
      const d = (from + ((i + 0.5) / HEAT_BINS) * span - t) / sigma
      if (d > -3 && d < 3) out[i] += Math.exp(-d * d)
    }
  const top = Math.max(1, ...out)
  return out.map(v => v / top)
}

const buildView = (m: ReplayMatch, pair: Pair | null, me: string, from: number, to: number): View => {
  const at = m.at ?? 0
  if (pair) {
    const r = pair.roster
    const bySlot = new Map(r.players.map(p => [p.slot, p]))
    const self = r.players.find(p => p.steamid === me)
    const moments: Moment[] = []
    const rounds: Round[] = []
    const all: number[] = []
    let us = 0
    let them = 0
    r.round_info!.forEach((ri, k) => {
      const t = ri.start * TICK + pair.offset
      const side = self?.stats?.lines?.[k]?.[4] ?? 0
      const winner = r.winners[k] ?? 0
      const won = !winner || !side ? 0 : winner === side ? 1 : -1
      if (won === 1) us++
      else if (won === -1) them++
      const next = r.round_info![k + 1] ? r.round_info![k + 1].start * TICK + pair.offset : Infinity
      if (t <= to && next > from + 1) rounds.push({ n: k + 1, t: Math.max(t, from), won, us, them, reason: ri.reason, side, dmg: self?.stats?.lines?.[k]?.[3] })
      for (const [tick, killer, victim, assister, flags, weapon] of ri.kills) {
        const kt = tick * TICK + pair.offset
        if (kt < from || kt > to) continue
        all.push(kt)
        const mine = killer === self?.slot
        const died = victim === self?.slot
        const helped = !mine && !died && assister === self?.slot
        if (!mine && !died && !helped) continue
        moments.push({
          t: kt,
          kind: mine ? 'kill' : died ? 'death' : 'assist',
          round: k + 1,
          killer: bySlot.get(killer),
          victim: bySlot.get(victim),
          assister: bySlot.get(assister),
          weapon,
          head: (flags & 1) !== 0,
          title: '',
          text: '',
          side: bySlot.get(victim)?.stats?.lines?.[k]?.[4],
        })
      }
      for (const [tick, slot, kind] of ri.bomb) {
        const bt = tick * TICK + pair.offset
        if (slot === self?.slot && kind < 2 && bt >= from && bt <= to) moments.push({ t: bt, kind: 'bomb', round: k + 1, killer: self, title: '', text: kind === 0 ? 'plant' : 'defuse' })
      }
    })
    for (const e of m.events) if (steamKind(e) === 'mark') moments.push({ t: at + e[0], kind: 'mark', round: 0, title: e[3], text: e[4] })
    moments.sort((a, b) => a.t - b.t)
    return { moments, rounds, heat: heatOf(all, from, to), me: self, pair }
  }
  const rounds: Round[] = [...steamRounds(m)].map(([n, t]) => ({ n, t: at + t, won: 0 })).sort((a, b) => a.t - b.t)
  const moments: Moment[] = []
  for (const e of m.events) {
    const k = steamKind(e)
    if (!k || k === 'round') continue
    const t = at + e[0]
    moments.push({ t, kind: k, round: rounds.filter(r => r.t <= t + 0.5).pop()?.n ?? 0, title: e[3], text: e[4] })
  }
  return { moments, rounds, heat: heatOf(moments.map(x => x.t), from, to), pair: null }
}

const deadTime = (m: ReplayMatch, pair: Pair, me: string, from: number, to: number) => {
  const r = pair.roster
  const at = m.at ?? 0
  const self = r.players.find(p => p.steamid === me)
  const info = r.round_info ?? []
  const starts = new Map<number, number[]>()
  for (const e of m.events) {
    const hit = /^cs2_(\d+)$/.exec(e[2])
    if (e[1] === 'event' && hit) starts.set(Number(hit[1]), [...(starts.get(Number(hit[1])) ?? []), at + e[0]])
  }
  const live = info.map(ri => ri.start * TICK + pair.offset)
  const end = info.map((ri, k) => (ri.decided >= 0 ? ri.decided * TICK + pair.offset : live[k + 1] ?? to))
  const freezeAt = (k: number) => {
    const list = (starts.get(k + 1) ?? []).filter(t => t <= live[k] && t > (k ? end[k - 1] : -Infinity))
    return list.length ? list[list.length - 1] : null
  }
  const out: Record<CutKind, Span[]> = { freeze: [], breaks: [], dead: [], outside: [] }
  info.forEach((ri, k) => {
    const f = freezeAt(k)
    if (f !== null) out.freeze.push([f, live[k]])
    if (k + 1 < info.length) out.breaks.push([end[k], freezeAt(k + 1) ?? live[k + 1]])
    const death = ri.kills.find(x => x[2] === self?.slot)
    if (death) out.dead.push([death[0] * TICK + pair.offset, end[k]])
  })
  if (info.length) {
    out.outside.push([from, freezeAt(0) ?? live[0]])
    out.outside.push([end[info.length - 1] + SCOREBOARD_TAIL, to])
  }
  for (const k of CUT_KINDS) out[k] = out[k].map(([a, b]): Span => [Math.max(a, from), Math.min(b, to)]).filter(([a, b]) => b > a)
  return out
}

const planCuts = (table: ChunkRow[], spans: Span[]) => {
  const merged = [...spans].sort((a, b) => a[0] - b[0]).reduce<Span[]>((acc, s) => {
    const last = acc[acc.length - 1]
    if (last && s[0] <= last[1]) last[1] = Math.max(last[1], s[1])
    else acc.push([s[0], s[1]])
    return acc
  }, [])
  const keep: number[] = []
  const cuts: Span[] = []
  let bytes = 0
  let seconds = 0
  for (const [idx, a, b, size] of table) {
    if (merged.some(([x, y]) => a >= x && b <= y)) {
      const last = cuts[cuts.length - 1]
      if (last && Math.abs(last[1] - a) < 0.01) last[1] = b
      else cuts.push([a, b])
      continue
    }
    keep.push(idx)
    bytes += size
    seconds += b - a
  }
  return { keep, cuts, bytes, seconds }
}

const mb = (bytes: number) => (bytes >= 1073741824 ? `${(bytes / 1073741824).toFixed(2)} GB` : `${Math.round(bytes / 1048576)} MB`)

function SaveBox(props: { table: ChunkRow[] | null; dead: Record<CutKind, Span[]> | null; onSave: (keep: number[], cuts: Span[]) => void; onClose: () => void }) {
  const { table, dead, onSave, onClose } = props
  const { t } = useUi()
  const [kinds, setKinds] = useState<CutKind[]>(() => {
    try {
      const v = JSON.parse(stored('replayCuts') ?? 'null')
      return Array.isArray(v) ? v.filter((k: CutKind) => CUT_KINDS.includes(k)) : CUT_DEFAULT
    } catch {
      return CUT_DEFAULT
    }
  })
  const total = useMemo(() => (table ? planCuts(table, []) : null), [table])
  const plan = useMemo(() => (table && dead ? planCuts(table, kinds.flatMap(k => dead[k])) : total), [table, dead, kinds, total])
  const each = useMemo(() => {
    const out: Partial<Record<CutKind, { bytes: number; seconds: number }>> = {}
    if (table && dead && total)
      for (const k of CUT_KINDS) {
        const p = planCuts(table, dead[k])
        out[k] = { bytes: total.bytes - p.bytes, seconds: total.seconds - p.seconds }
      }
    return out
  }, [table, dead, total])
  return (
    <div className="rp-savebox">
      <strong>{t('replaySaveTitle')}</strong>
      {dead ? (
        CUT_KINDS.map(k => {
          const on = kinds.includes(k)
          const e = each[k]
          return (
            <label key={k}>
              <span>
                {t(CUT_LABEL[k])}
                <small>{e ? t('replayCutSaves', { size: mb(e.bytes), time: clock(e.seconds) }) : '…'}</small>
              </span>
              <button
                className={`rp-switch${on ? ' on' : ''}`}
                role="switch"
                aria-checked={on}
                onClick={() => {
                  const next = on ? kinds.filter(x => x !== k) : [...kinds, k]
                  setKinds(next)
                  store('replayCuts', JSON.stringify(next))
                }}
              >
                <span />
              </button>
            </label>
          )
        })
      ) : (
        <p className="rp-savebox-note">{t('replayCutNoDemo')}</p>
      )}
      <div className="rp-savebox-sum">
        {plan && total ? (
          <>
            <b>{mb(plan.bytes)}</b>
            <span>{t('replaySaveOf', { size: mb(total.bytes), time: clock(plan.seconds), all: clock(total.seconds) })}</span>
          </>
        ) : (
          <span>{t('replaySaveCounting')}</span>
        )}
      </div>
      <div className="rp-savebox-btns">
        <button className="ghost" onClick={onClose}>
          {t('cancel')}
        </button>
        <button className="rp-suggest-keep" disabled={!plan || !plan.keep.length} onClick={() => plan && onSave(plan.keep, plan.cuts)}>
          {t('replaySave')}
        </button>
      </div>
    </div>
  )
}

type ClipSpec = { height: number; width: number; fps: number; bitrate: number; bytes: number; rough: boolean; long: boolean }

const clipSpec = (max: number, dur: number, sw: number, sh: number, px: number): ClipSpec => {
  const full = Math.round(px * 60 * BPP_ORIGINAL)
  const original = { height: sh, width: sw, fps: 60, bitrate: full, bytes: ((full + AUDIO_BPS) * dur) / 8, rough: false, long: false }
  const video = (max * 8 * SIZE_SAFETY) / Math.max(1, dur) - AUDIO_BPS
  if (!max || video >= full) return original
  const options = [[sh, 60], [sh, 30], ...(sh > MIN_HEIGHT ? [[MIN_HEIGHT, 30]] : [])]
  const bpp = (h: number, fps: number) => video / (px * (h / sh) ** 2 * fps)
  for (const [h, fps] of options) {
    if (bpp(h, fps) >= BPP_MIN) return { height: h, width: Math.round((sw * h) / sh), fps, bitrate: Math.round(video), bytes: max * SIZE_SAFETY, rough: false, long: false }
  }
  const [h, fps] = options[options.length - 1]
  return { height: h, width: Math.round((sw * h) / sh), fps, bitrate: Math.round(Math.max(video, 0)), bytes: max * SIZE_SAFETY, rough: true, long: bpp(h, fps) < BPP_FLOOR }
}

const megabytes = (bytes: number) => `${bytes >= 10e6 ? Math.round(bytes / 1e6) : (bytes / 1e6).toFixed(1)} MB`

function ClipEditor(props: {
  range: Span
  setRange: (r: Span) => void
  from: number
  to: number
  at: number
  playing: boolean
  ticker: { current: ((t: number) => void) | null }
  sw: number
  sh: number
  px: number
  onSeek: (v: number, play?: boolean) => void
  onPause: () => void
  onMake: (preset: string, spec: ClipSpec) => void
  onClose: () => void
}) {
  const { range, setRange, from, to, at, playing, ticker, sw, sh, px, onSeek, onPause, onMake, onClose } = props
  const head = useRef<HTMLSpanElement>(null)
  const { t } = useUi()
  const zoom = useRef<HTMLDivElement>(null)
  const drag = useRef<{ kind: 0 | 1 | 2; x: number; r: Span } | null>(null)
  const frameOf = (r: Span): Span => {
    const pad = Math.max(CLIP_PAD, (r[1] - r[0]) * 0.4)
    return [Math.max(from, r[0] - pad), Math.min(to, r[1] + pad)]
  }
  const [view, setView] = useState<Span>(() => frameOf(range))
  const [preview, setPreview] = useState(false)
  const [menu, setMenu] = useState(false)
  const [preset, setPreset] = useState(() => stored('clipPreset') ?? 'discord20')
  const dur = range[1] - range[0]
  const specs = PRESETS.map(p => ({ ...p, spec: clipSpec(p.max, dur, sw, sh, px) }))
  const chosen = specs.find(p => p.id === preset && !p.spec.long) ?? specs[0]
  const full = clipSpec(0, dur, sw, sh, px)
  const describe = (s: ClipSpec, max: number) =>
    s.long ? t('clipTooLong') : `${s.height}p ${s.fps} fps, ${s.bitrate === full.bitrate ? `~${megabytes(s.bytes)}` : t('clipUpTo', { size: megabytes(max) })}${s.rough ? `, ${t('clipRough')}` : ''}`

  useEffect(() => {
    if (!drag.current) setView(frameOf(range))
  }, [range[0], range[1]])

  useEffect(() => {
    if (preview && !playing) setPreview(false)
  }, [preview, playing])

  const pos = (v: number) => ((v - view[0]) / Math.max(0.001, view[1] - view[0])) * 100
  ticker.current = v => {
    const el = head.current
    if (el) {
      el.style.display = v >= view[0] && v <= view[1] ? '' : 'none'
      el.style.left = `${pos(v)}%`
    }
    if (preview && playing && v >= range[1]) onSeek(range[0], true)
  }
  useEffect(() => () => {
    ticker.current = null
  }, [])
  const timeAt = (clientX: number) => {
    const r = zoom.current!.getBoundingClientRect()
    return view[0] + Math.min(1, Math.max(0, (clientX - r.left) / r.width)) * (view[1] - view[0])
  }
  const grab = (kind: 0 | 1 | 2) => (e: PointerEvent<HTMLElement>) => {
    e.stopPropagation()
    e.currentTarget.setPointerCapture(e.pointerId)
    drag.current = { kind, x: timeAt(e.clientX), r: range }
  }
  const move = (e: PointerEvent<HTMLElement>) => {
    const d = drag.current
    if (!d) return
    const v = timeAt(e.clientX)
    if (d.kind === 0) setRange([Math.max(from, Math.min(v, d.r[1] - CLIP_MIN)), d.r[1]])
    else if (d.kind === 1) setRange([d.r[0], Math.min(to, Math.max(v, d.r[0] + CLIP_MIN))])
    else {
      const shift = Math.min(Math.max(v - d.x, from - d.r[0]), to - d.r[1])
      setRange([d.r[0] + shift, d.r[1] + shift])
    }
  }
  const drop = () => {
    drag.current = null
    setView(frameOf(range))
  }
  const step = view[1] - view[0] > 90 ? 10 : view[1] - view[0] > 30 ? 5 : 1
  const ticks: number[] = []
  for (let v = Math.ceil((view[0] - at) / step) * step + at; v <= view[1]; v += step) ticks.push(v)

  return (
    <div className="rp-clipper">
      <div className="rp-clipzoom" ref={zoom} onPointerDown={e => onSeek(timeAt(e.clientX))}>
        {ticks.map(v => {
          const big = Math.round(v - at) % (step * 5) === 0
          return (
            <i key={v} className={big ? 'big' : ''} style={{ left: `${pos(v)}%` }}>
              {big && <small>{clock(v - at)}</small>}
            </i>
          )
        })}
        <span className="rp-clipshade" style={{ left: 0, width: `${pos(range[0])}%` }} />
        <span className="rp-clipshade" style={{ left: `${pos(range[1])}%`, right: 0 }} />
        <span
          className="rp-clipwin"
          style={{ left: `${pos(range[0])}%`, width: `${pos(range[1]) - pos(range[0])}%` }}
          onPointerDown={grab(2)}
          onPointerMove={move}
          onPointerUp={drop}
        >
          <i className="rp-cliphandle start" onPointerDown={grab(0)} onPointerMove={move} onPointerUp={drop}>
            <small>{preciseClock(range[0] - at)}</small>
          </i>
          <i className="rp-cliphandle end" onPointerDown={grab(1)} onPointerMove={move} onPointerUp={drop}>
            <small>{preciseClock(range[1] - at)}</small>
          </i>
        </span>
        <span className="rp-cliphead" ref={head} />
      </div>
      <div className="rp-clipctl">
        <button
          className={`rp-ctl${preview ? ' on' : ''}`}
          onClick={() => {
            if (preview) {
              setPreview(false)
              onPause()
            } else {
              setPreview(true)
              onSeek(range[0], true)
            }
          }}
          title={t('clipPreview')}
        >
          {preview ? <PauseIcon /> : <PlayIcon />}
        </button>
        <span className="rp-cliplen">{dur.toFixed(1)} s</span>
        <div className="rp-presetpick">
          <button className={menu ? 'on' : ''} onClick={() => setMenu(v => !v)} aria-expanded={menu}>
            <span>{t(chosen.label)}</span>
            <small>{describe(chosen.spec, chosen.max)}</small>
            <ChevronDownIcon />
          </button>
          {menu && (
            <div className="rp-presetmenu" role="listbox">
              {specs.map(p => (
                <button
                  key={p.id}
                  role="option"
                  aria-selected={p.id === chosen.id}
                  className={p.id === chosen.id ? 'on' : ''}
                  disabled={p.spec.long}
                  onClick={() => {
                    setPreset(p.id)
                    store('clipPreset', p.id)
                    setMenu(false)
                  }}
                >
                  <span>{t(p.label)}</span>
                  <small>{describe(p.spec, p.max)}</small>
                </button>
              ))}
            </div>
          )}
        </div>
        <span className="rp-clipgrow" />
        <button className="ghost" onClick={onClose}>
          {t('cancel')}
        </button>
        <button className="rp-clipgo" disabled={chosen.spec.long} onClick={() => onMake(chosen.id, chosen.spec)}>
          <ScissorsIcon />
          {t('clipMake')}
        </button>
      </div>
    </div>
  )
}

function ClipToast(props: { clip: Clip; onOpen: () => void; onClose: () => void }) {
  const { clip, onOpen, onClose } = props
  const { t } = useUi()
  return (
    <div className={`rp-cliptoast ${clip.state}`}>
      {clip.state === 'making' && <i className="rp-cliptoast-bar" style={{ width: `${Math.round(clip.p * 100)}%` }} />}
      <span className="rp-cliptoast-text">
        <strong>{clip.state === 'making' ? t('clipMaking', { n: Math.round(clip.p * 100) }) : clip.state === 'done' ? t('clipDone') : t('clipFailed', { err: clip.err })}</strong>
        <small>{clip.state === 'done' ? `${clip.name}, ${megabytes(clip.bytes)}` : clip.name}</small>
      </span>
      {clip.state === 'making' && (
        <button className="rp-ctl" onClick={() => send({ type: 'clip_cancel', id: clip.id })} title={t('cancel')}>
          <CloseIcon />
        </button>
      )}
      {clip.state === 'done' && (
        <>
          <button className="ghost" onClick={() => send({ type: 'clip_copy', paths: [clip.path] })}>
            <CopyIcon />
            {t('clipCopyShort')}
          </button>
          <button className="ghost" onClick={onOpen}>
            {t('clipOpen')}
          </button>
        </>
      )}
      {clip.state !== 'making' && (
        <button className="rp-ctl" onClick={onClose} aria-label={t('close')}>
          <CloseIcon />
        </button>
      )}
    </div>
  )
}

const KILL_HEAD = 1
const KILL_WALL = 2
const KILL_SMOKE = 4
const KILL_BLIND = 8
const KILL_NOSCOPE = 16

const clipTitle = (m: ReplayMatch, pair: Pair | null, me: string, range: Span, t: T) => {
  const self = pair?.roster.players.find(p => p.steamid === me)
  if (!pair || !self) return m.map
  const r = pair.roster
  const vt = (tick: number) => tick * TICK + pair.offset
  let best = { rank: -1, label: '' }
  ;(r.round_info ?? []).forEach((ri, k) => {
    const mine = ri.kills.filter(x => x[1] === self.slot && x[2] !== self.slot && vt(x[0]) >= range[0] - 0.5 && vt(x[0]) <= range[1] + 0.5)
    if (!mine.length) return
    const playing = (p: Player) => !!p.stats?.lines?.[k]?.[0]
    const mates = r.players.filter(p => p.team === self.team && playing(p)).map(p => p.slot)
    const foes = r.players.filter(p => p.team !== self.team && p.team >= 0 && playing(p)).map(p => p.slot)
    const dead = new Set<number>()
    let against = 0
    for (const x of ri.kills) {
      dead.add(x[2])
      const left = mates.filter(s => !dead.has(s))
      if (left.length === 1 && left[0] === self.slot) {
        against = foes.filter(s => !dead.has(s)).length
        break
      }
    }
    const won = r.winners[k] === self.stats?.lines?.[k]?.[4]
    const clutch = won && against >= 2 ? against : 0
    const weapons = new Set(mine.map(x => weaponName(x[5])))
    const gun = weapons.size === 1 ? ` ${[...weapons][0]}` : ''
    const n = mine.length
    let rank = 0
    let label = ''
    if (n >= 5) {
      rank = 100 + clutch
      label = clutch ? `ACE ${t('clipClutch')} 1v${clutch}` : 'ACE'
    } else if (clutch) {
      rank = 50 + clutch
      label = `${t('clipClutch')} 1v${clutch}`
    } else if (n >= 2) {
      rank = 10 * n
      label = `${n}K${gun}`
    } else {
      const f = mine[0][4]
      const w = weaponName(mine[0][5])
      rank = f & (KILL_NOSCOPE | KILL_WALL | KILL_SMOKE | KILL_BLIND) ? 5 : f & KILL_HEAD ? 2 : 1
      label =
        f & KILL_NOSCOPE
          ? `Noscope ${w}`
          : f & KILL_WALL
            ? `Wallbang ${w}`
            : f & KILL_SMOKE
              ? `${w} ${t('clipSmoke')}`
              : f & KILL_BLIND
                ? `${w} ${t('clipBlind')}`
                : f & KILL_HEAD
                  ? `${w} headshot`
                  : `${w} kill`
    }
    if (rank > best.rank) best = { rank, label }
  })
  return best.label ? `${best.label} ${m.map}` : m.map
}

export const replayScore = (m: ReplayMatch, demos: Demo[] | null, rosters: Record<string, Roster>, me: string) => {
  const pair = pairDemo(m, demos ?? [], rosters, me)
  const self = pair?.roster.players.find(p => p.steamid === me)
  if (pair && self) return { pair, a: pair.roster.teams[self.team]?.score ?? 0, b: pair.roster.teams[1 - self.team]?.score ?? 0 }
  const [a, b] = m.score.split(':').map(x => Number(x.trim()))
  return { pair, a, b }
}

const KEEP_OPTIONS = [3, 6, 10, 20]

const gb = (bytes: number) => {
  const g = bytes / 1073741824
  return g < 1 ? `${Math.max(1, Math.round(bytes / 1048576))} MB` : `${g < 10 ? g.toFixed(1) : Math.round(g)} GB`
}

const hours = (secs: number, t: T) => {
  const h = secs / 3600
  return h >= 1.5 ? t('rsHours', { n: Math.round(h) }) : t('rsMinutes', { n: Math.max(1, Math.round(secs / 60)) })
}

export const keptBy = (matches: ReplayMatch[], n: number) => {
  const out = new Set<string>()
  for (const m of matches) {
    if (out.size >= n) break
    if (m.saved || (m.video && m.done)) out.add(m.id)
  }
  return out
}

export function ReplayFirst(props: {
  data: ReplaysData
  art: Record<string, boolean>
  demos: Demo[] | null
  rosters: Record<string, Roster>
  me: string
  onDone: (n: number) => void
}) {
  const { t } = useUi()
  const { data, art, demos, rosters, me, onDone } = props
  const [n, setN] = useState(10)
  const per = (data.rate ?? 0) * (data.length ?? 0)
  const drive = data.saved_dir?.slice(0, 2) ?? ''
  const back = data.matches.find(m => art[mapId(m.map)])
  return (
    <div className="rp rp-first">
      <div className="rp-left">
        <section className="rp-first-stage">
          {back && <img className="rp-first-art" src={`https://cs2voice-maps.example/${mapId(back.map)}.png`} alt="" />}
          <div className="rp-first-copy">
            <h1>{t('rsTitle')}</h1>
            <p>{data.buffer ? t('rsText', { time: hours(data.buffer, t) }) : t('rsTextPlain')}</p>
            <div className="rp-first-pick">
              <div className="choices" role="radiogroup" aria-label={t('rsTitle')}>
                {KEEP_OPTIONS.map(k => (
                  <button key={k} role="radio" aria-checked={n === k} className={`choice${n === k ? ' on' : ''}`} onClick={() => setN(k)}>
                    {k}
                  </button>
                ))}
              </div>
              {per > 0 && <span>{t('rsSize', { size: gb(per * n), free: gb(data.free ?? 0), drive })}</span>}
            </div>
            <div className="rp-first-go">
              <button className="rp-first-keep" onClick={() => onDone(n)} autoFocus>
                {t('rsKeep', { n })}
              </button>
              <button className="ghost" onClick={() => onDone(0)}>
                {t('rsNo')}
              </button>
            </div>
          </div>
        </section>
      </div>
      <aside className="rp-right picking">
        <div className="rp-picker">
          <ReplayNotice data={data} />
          <ReplayList data={data} selected={null} onSelect={() => {}} art={art} demos={demos} rosters={rosters} me={me} keep={keptBy(data.matches, n)} />
        </div>
      </aside>
    </div>
  )
}

export function ReplayNotice(props: { data: ReplaysData | null }) {
  const { t } = useUi()
  const { data } = props
  if (!data || data.background === 1) return null
  return (
    <div className="rp-notice">
      <strong>{data.folder ? t('replaysSteamOff') : t('replaysNoFolder')}</strong>
      <span>{t('replaysSteamHow')}</span>
    </div>
  )
}

export function ReplayList(props: {
  data: ReplaysData | null
  selected: string | null
  onSelect: (id: string) => void
  art: Record<string, boolean>
  demos: Demo[] | null
  rosters: Record<string, Roster>
  me: string
  keep?: Set<string>
}) {
  const { t, lang } = useUi()
  const { data, selected, onSelect, art, demos, rosters, me, keep } = props
  const scores = useMemo(() => new Map((data?.matches ?? []).map(m => [m.id, replayScore(m, demos, rosters, me)])), [data, demos, rosters, me])
  useEffect(() => {
    for (const m of data?.matches ?? []) send({ type: 'art', map: mapId(m.map) })
  }, [data])
  const [tab, setTab] = useState<ListTab>(() => {
    const v = stored('replayTab')
    return v === 'fav' || v === 'saved' ? v : 'latest'
  })
  const [favs, setFavs] = useState<string[]>(() => {
    try {
      const v = JSON.parse(stored('replayFav') ?? '[]')
      return Array.isArray(v) ? v : []
    } catch {
      return []
    }
  })
  const pick = (v: ListTab) => {
    setTab(v)
    store('replayTab', v)
  }
  const star = (id: string) => {
    const next = favs.includes(id) ? favs.filter(x => x !== id) : [...favs, id]
    setFavs(next)
    store('replayFav', JSON.stringify(next))
  }
  const all = data?.matches ?? []
  const shown = keep ? all : tab === 'fav' ? all.filter(m => favs.includes(m.id)) : tab === 'saved' ? all.filter(m => m.saved) : all
  const tabs: { id: ListTab; label: Key; n: number }[] = [
    { id: 'latest', label: 'replayTabLatest', n: all.length },
    { id: 'fav', label: 'replayTabFav', n: all.filter(m => favs.includes(m.id)).length },
    { id: 'saved', label: 'replayTabSaved', n: all.filter(m => m.saved).length },
  ]
  return (
    <>
      {!keep && <div className="rp-tabs" role="tablist">
        {tabs.map(x => (
          <button key={x.id} role="tab" aria-selected={tab === x.id} className={tab === x.id ? 'on' : ''} onClick={() => pick(x.id)}>
            {t(x.label)}
            <small>{x.n}</small>
          </button>
        ))}
      </div>}
      {data && !shown.length && (
        <div className="list-empty">{t(tab === 'fav' ? 'replayFavEmpty' : tab === 'saved' ? 'savedNone' : 'replaysEmpty')}</div>
      )}
      <div className="list" role="list">
        {shown.map(m => {
          const id = mapId(m.map)
          const { pair, a, b } = scores.get(m.id)!
          return (
            <div
              key={m.id}
              role="listitem"
              tabIndex={0}
              className={`item rp-item${m.id === selected ? ' on' : ''}${m.video ? '' : ' gone'}${keep?.has(m.id) ? ' keep' : ''}`}
              onClick={() => onSelect(m.id)}
              onKeyDown={e => e.key === 'Enter' && onSelect(m.id)}
            >
              {art[id] && <img className="item-art" src={`https://cs2voice-maps.example/${id}.png`} alt="" />}
              <span className="item-map rp-item-top">
                <span>{m.map}</span>
                {pair ? (
                  <span className="item-score">
                    <span className="t">{pair.roster.teams[0].score}</span>
                    <span className="sep">:</span>
                    <span className="ct">{pair.roster.teams[1].score}</span>
                  </span>
                ) : (
                  <span className="item-score rp-item-steam">
                    {a}
                    <span className="sep">:</span>
                    {b}
                  </span>
                )}
                <button
                  className={`rp-star${favs.includes(m.id) ? ' on' : ''}`}
                  aria-label={t('replayFavToggle')}
                  aria-pressed={favs.includes(m.id)}
                  onClick={e => {
                    e.stopPropagation()
                    star(m.id)
                  }}
                >
                  <StarIcon />
                </button>
              </span>
              <span className="item-meta">{when(m.unix, t, lang)}</span>
              <span className="item-tags">
                <span className="tag">{m.mode}</span>
                {m.kda && <span className="tag">{m.kda}</span>}
                {pair && <span className="tag voice">{t('replayDemo')}</span>}
                {m.saved && <span className="tag">{t(m.auto ? 'replayAutoTag' : 'replaySavedTag')}</span>}
                {keep?.has(m.id) && !m.saved && <span className="tag rp-keep-tag">{t('replaySavedTag')}</span>}
                {!m.video && <span className="tag mute">{t('replayGone')}</span>}
              </span>
            </div>
          )
        })}
      </div>
    </>
  )
}

const clampRate = (v: number) => Math.round(Math.min(MAX_RATE, Math.max(MIN_RATE, v)) * 100) / 100

const ratePos = (v: number) => Math.log(v / MIN_RATE) / Math.log(MAX_RATE / MIN_RATE)
const rateAt = (p: number) => {
  const v = MIN_RATE * Math.pow(MAX_RATE / MIN_RATE, p)
  const near = RATE_TICKS.find(k => Math.abs(ratePos(k) - p) < 0.025)
  return near ?? clampRate(v)
}
const rateText = (v: number) => `${Number(v.toFixed(2))}x`

const preciseClock = (secs: number) => {
  const s = Math.max(0, secs)
  const m = Math.floor(s / 60)
  return `${m}:${(s - m * 60).toFixed(3).padStart(6, '0')}`
}

function RateSlider(props: { value: number; onChange: (v: number) => void; title: string }) {
  const { value, onChange, title } = props
  const pos = ratePos(value)
  return (
    <span className="rp-rate" title={title}>
      <span className="rp-rate-track">
        <input
          type="range"
          min={0}
          max={1000}
          value={Math.round(pos * 1000)}
          onChange={e => onChange(rateAt(Number(e.target.value) / 1000))}
          onDoubleClick={() => onChange(1)}
          onWheel={e => {
            const i = RATE_TICKS.findIndex(k => k >= value - 0.001)
            const next = e.deltaY < 0 ? RATE_TICKS[Math.min(RATE_TICKS.length - 1, RATE_TICKS[i] > value + 0.001 ? i : i + 1)] : RATE_TICKS[Math.max(0, i - 1)]
            if (next) onChange(next)
          }}
          style={{ ['--v' as string]: `${pos * 100}%` }}
        />
        {RATE_TICKS.map(k => (
          <i key={k} className={k === 1 ? 'one' : ''} style={{ left: `${ratePos(k) * 100}%` }} />
        ))}
      </span>
      <b>{rateText(value)}</b>
    </span>
  )
}

function Glyph(props: { kind: string; icons: boolean }) {
  const { kind, icons } = props
  const name = GLYPHS[kind]
  if (icons && name) {
    const url = `url(${ICONS}${name}.svg)`
    return <i className="rp-glyph" style={{ WebkitMaskImage: url, maskImage: url }} />
  }
  return kind === 'death' ? <SkullIcon /> : kind === 'bomb' ? <BombIcon /> : kind === 'mark' ? <BookmarkIcon /> : kind === 'assist' ? <UsersIcon /> : <TargetIcon />
}

function Face(props: { p?: Player; avatars: Record<string, string>; side?: number; small?: boolean }) {
  const { p, avatars, side, small } = props
  return (
    <span className={`rp-face${small ? ' small' : ''}${side === 2 ? ' t' : side === 3 ? ' ct' : ''}`} title={p?.name}>
      <Avatar url={p ? avatars[p.steamid] : undefined} name={p?.name ?? '?'} />
    </span>
  )
}

function MomentRow(props: { m: Moment; avatars: Record<string, string>; icons: boolean; at: number; now?: boolean; cut?: boolean; onClick?: () => void; onClip?: () => void }) {
  const { m, avatars, icons, at, now, cut, onClick, onClip } = props
  const { t } = useUi()
  const rich = !!(m.killer || m.victim)
  let body: ReactNode
  if (m.kind === 'mark')
    body = (
      <span className="rp-row-plain">
        <span className="rp-row-icon">
          <BookmarkIcon />
        </span>
      </span>
    )
  else if (m.kind === 'bomb' && rich)
    body = (
      <span className="rp-row-main">
        <span className="rp-assist" />
        <Face p={m.killer} avatars={avatars} />
        <Icon name={m.text === 'plant' ? 'icons_equipment_c4' : 'icons_equipment_defuser'} ready={icons} className="rp-weapon" />
        <span className="rp-row-label">{t(m.text === 'plant' ? 'replayPlant' : 'replayDefuse')}</span>
      </span>
    )
  else if (rich)
    body = (
      <span className="rp-row-main">
        <span className="rp-assist">{m.assister && <Face p={m.assister} avatars={avatars} small />}</span>
        <Face p={m.killer} avatars={avatars} />
        {m.weapon ? <Icon name={weaponIcon(m.weapon)} ready={icons} className="rp-weapon" /> : <span className="rp-weapon" />}
        <span className="rp-hs">{m.head && <Icon name="hud_deathnotice_icon_headshot" ready={icons} />}</span>
        <Face p={m.victim} avatars={avatars} side={m.side} />
      </span>
    )
  else
    body = (
      <span className="rp-row-plain">
        <span className="rp-row-icon">
          <Glyph kind={m.kind} icons={icons} />
        </span>
        <span className="rp-row-text">
          <strong>{m.title}</strong>
          {m.text && <span>{m.text}</span>}
        </span>
      </span>
    )
  return (
    <div className="rp-rowwrap">
      <button className={`rp-row ${m.kind}${now ? ' now' : ''}${cut ? ' cut' : ''}`} onClick={onClick}>
        {body}
        <span className="rp-row-time">{clock(m.t - at)}</span>
      </button>
      {onClip && (
        <button className="rp-rowclip" onClick={onClip} title={t('clipThis')}>
          <ScissorsIcon />
        </button>
      )}
    </div>
  )
}

export function ReplayView(props: {
  m: ReplayMatch
  player: PlayerState | null
  demos: Demo[] | null
  rosters: Record<string, Roster>
  me: string
  avatars: Record<string, string>
  icons: boolean
  fullscreen: boolean
  picker: ReactNode
  count: number
  saving?: number
  table: ChunkRow[] | null
  clips: Clip[]
  onClip: (c: Clip) => void
  onOpenClips: () => void
  startPicking: boolean
  start?: number
  keep: number
  onSave: (keep: number[], cuts: Span[]) => void
}) {
  const { t } = useUi()
  const { m, player, demos, rosters, me, avatars, icons, fullscreen, picker, count, saving, table, clips, onClip, onOpenClips, startPicking, start: startAt, keep, onSave } = props
  const [range, setRange] = useState<Span | null>(null)
  const [toast, setToast] = useState<string | null>(null)
  const toastClip = toast ? clips.find(c => c.id === toast) ?? null : null
  const [saveOpen, setSaveOpen] = useState(false)
  const [armed, setArmed] = useState(false)
  const opened = useRef<string | null>(null)

  useEffect(() => {
    if (!armed) return
    const id = window.setTimeout(() => setArmed(false), 3000)
    return () => clearTimeout(id)
  }, [armed])
  const [picking, setPicking] = useState(startPicking)

  useEffect(() => {
    if (startPicking) setPicking(true)
  }, [startPicking])
  const stage = useRef<HTMLDivElement>(null)
  const track = useRef<HTMLDivElement>(null)
  const list = useRef<HTMLDivElement>(null)
  const at = m.at ?? 0
  const from = Math.max(m.from ?? 0, at)
  const to = Math.min(m.to ?? Infinity, at + m.duration)
  const cuts = m.cuts ?? []
  const span = Math.max(1, to - from)
  const pair = useMemo(() => pairDemo(m, demos ?? [], rosters, me), [m, demos, rosters, me])
  const view = useMemo(() => buildView(m, pair, me, from, to), [m, pair, me, from, to])
  const [now, setNow] = useState(from)
  const fillEl = useRef<HTMLSpanElement>(null)
  const knobEl = useRef<HTMLSpanElement>(null)
  const timeEl = useRef<HTMLSpanElement>(null)
  const frameEl = useRef<HTMLSpanElement>(null)
  const clipTicker = useRef<((t: number) => void) | null>(null)
  const paint = useRef<(t: number) => void>(() => {})
  const last = useRef({ t: from, at: performance.now(), playing: false })
  const [hover, setHover] = useState<number | null>(null)
  const [peek, setPeek] = useState<number | null>(null)
  const peekTimer = useRef(0)
  const holdPeek = (i: number, now?: boolean) => {
    clearTimeout(peekTimer.current)
    if (now || peek !== null) setPeek(i)
    else peekTimer.current = window.setTimeout(() => setPeek(i), PEEK_MS)
  }
  const dropPeek = () => {
    clearTimeout(peekTimer.current)
    peekTimer.current = window.setTimeout(() => setPeek(null), 220)
  }
  const [width, setWidth] = useState(800)
  const [highlight, setHighlight] = useState(false)
  const [hlKinds, setHlKinds] = useState<Kind[]>(() => {
    try {
      const v = JSON.parse(stored('replayHl') ?? 'null')
      return Array.isArray(v) ? v.filter((k: Kind) => HL_KINDS.includes(k)) : HL_DEFAULT
    } catch {
      return HL_DEFAULT
    }
  })
  const [hlMenu, setHlMenu] = useState(false)
  const [roundMenu, setRoundMenu] = useState(false)
  const [dismissed, setDismissed] = useState(() => stored(`replayDismiss:${m.id}`) === '1')
  const busy = saving !== undefined && saving >= 0
  const roundBox = useRef<HTMLSpanElement>(null)
  const [volume, setVolume] = useState(() => Number(stored('replayVolume') ?? '1'))
  const [muted, setMuted] = useState(false)
  const [rate, setRate] = useState(() => clampRate(Number(stored('replayRate') ?? '1') || 1))
  const playable = !!m.video && m.codec === 'hevc'
  const playing = player?.state === 'playing'

  const [idle, setIdle] = useState(false)
  const [panel, setPanel] = useState(() => stored('replayPanel') !== '0')
  const [stretchPick, setStretchPick] = useState(() => stored('replayStretch'))
  const narrow = !!player?.width && !!player?.height && player.width / player.height < 1.5
  const stretch = stretchPick === null ? narrow : stretchPick === '1'
  const click = useRef(0)
  const [dip, setDip] = useState(false)
  const [curtain, setCurtain] = useState(false)
  const [caption, setCaption] = useState<{ key: number; title: string; sub: string } | null>(null)
  const awaiting = useRef<number | null>(null)
  const jumping = useRef(false)
  const dipTimer = useRef(0)
  const captionTimer = useRef(0)
  const rect = useRef<number[] | null>(null)
  const tween = useRef(0)
  const tweenNext = useRef(false)

  useEffect(() => {
    document.documentElement.classList.add('video-on')
    return () => {
      document.documentElement.classList.remove('video-on')
      send({ type: 'win', action: 'windowed' })
    }
  }, [])

  useEffect(() => {
    document.documentElement.classList.toggle('fs', fullscreen)
    return () => document.documentElement.classList.remove('fs')
  }, [fullscreen])

  useEffect(() => {
    document.documentElement.classList.toggle('idle', idle && fullscreen)
    return () => document.documentElement.classList.remove('idle')
  }, [idle, fullscreen])

  useEffect(() => {
    if (!fullscreen) return
    let timer = 0
    const wake = () => {
      setIdle(false)
      clearTimeout(timer)
      timer = window.setTimeout(() => setIdle(true), 2500)
    }
    wake()
    window.addEventListener('pointermove', wake)
    window.addEventListener('pointerdown', wake)
    return () => {
      clearTimeout(timer)
      window.removeEventListener('pointermove', wake)
      window.removeEventListener('pointerdown', wake)
      setIdle(false)
    }
  }, [fullscreen])

  useEffect(() => {
    if (!icons) send({ type: 'icons' })
  }, [icons])

  useEffect(() => {
    const r = pair?.roster
    if (r?.players.length) send({ type: 'profiles', ids: r.players.map(p => p.steamid) })
  }, [pair])

  useEffect(() => {
    if (!playable) return
    const start = startAt !== undefined && opened.current !== m.id ? Math.min(Math.max(startAt, from), to) : opened.current === m.id ? Math.min(Math.max(last.current.t, from), to) : from
    opened.current = m.id
    send({ type: 'player_open', video: m.video!, t: start })
    send({ type: 'player_rate', v: rate })
    last.current = { t: start, at: performance.now(), playing: false }
    setNow(start)
    return () => send({ type: 'player_close' })
  }, [m.id, m.video, playable, from])

  useEffect(() => {
    send({ type: 'player_rate', v: rate })
    store('replayRate', String(rate))
  }, [rate])

  useEffect(() => {
    send({ type: 'player_volume', v: muted ? 0 : volume })
    store('replayVolume', String(volume))
  }, [volume, muted])

  useEffect(() => {
    if (!player) return
    last.current = { t: player.t, at: performance.now(), playing: player.state === 'playing' }
    setNow(player.t)
    const want = awaiting.current
    if (want !== null && Math.abs(player.t - want) < 0.6) {
      awaiting.current = null
      jumping.current = false
      clearTimeout(dipTimer.current)
      setDip(false)
    }
  }, [player])

  useEffect(() => {
    if (!curtain) return
    const id = window.setTimeout(() => setCurtain(false), 50)
    return () => clearTimeout(id)
  }, [fullscreen])

  useEffect(() => {
    if (!curtain) return
    const id = window.setTimeout(() => setCurtain(false), 900)
    return () => clearTimeout(id)
  }, [curtain])

  useLayoutEffect(() => {
    paint.current(playing ? last.current.t + ((performance.now() - last.current.at) / 1000) * rate : now)
  }, [now, width, from, to, range, playing])

  useEffect(() => {
    if (!playing) return
    let id = 0
    let shown = -1
    const step = () => {
      const l = last.current
      const t = l.t + (l.playing ? ((performance.now() - l.at) / 1000) * rate : 0)
      paint.current(t)
      const q = Math.floor(t * NOW_HZ)
      if (q !== shown) {
        shown = q
        setNow(t)
      }
      id = requestAnimationFrame(step)
    }
    id = requestAnimationFrame(step)
    return () => cancelAnimationFrame(id)
  }, [playing, rate])

  useLayoutEffect(() => {
    const el = stage.current
    if (!el || !playable) return
    const apply = (v: number[]) => send({ type: 'player_rect', x: v[0], y: v[1], w: v[2], h: v[3] })
    const place = () => {
      const r = el.getBoundingClientRect()
      const vw = stretch ? window.screen.width : player?.width || 4
      const vh = stretch ? window.screen.height : player?.height || 3
      const s = Math.min(r.width / vw, r.height / vh)
      const w = vw * s
      const h = vh * s
      const d = devicePixelRatio
      const target = [Math.round((r.left + (r.width - w) / 2) * d), Math.round((r.top + (r.height - h) / 2) * d), Math.round(w * d), Math.round(h * d)]
      cancelAnimationFrame(tween.current)
      const startRect = rect.current
      if (!tweenNext.current || !startRect) {
        rect.current = target
        apply(target)
        return
      }
      tweenNext.current = false
      const t0 = performance.now()
      const step = () => {
        const k = Math.min(1, (performance.now() - t0) / TWEEN_MS)
        const e = 1 - Math.pow(1 - k, 3)
        const v = startRect.map((a, i) => Math.round(a + (target[i] - a) * e))
        rect.current = v
        apply(v)
        if (k < 1) tween.current = requestAnimationFrame(step)
      }
      step()
    }
    place()
    const ro = new ResizeObserver(place)
    ro.observe(el)
    return () => ro.disconnect()
  }, [playable, player?.width, player?.height, m.id, stretch])

  useLayoutEffect(() => {
    const el = track.current
    if (!el) return
    const ro = new ResizeObserver(() => setWidth(el.clientWidth))
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const seek = (v: number, play?: boolean) => {
    const target = Math.min(Math.max(v, from), to)
    last.current = { t: target, at: performance.now(), playing: last.current.playing }
    setNow(target)
    send({ type: 'player_seek', t: target })
    if (play && !playing) send({ type: 'player_play' })
  }
  const toggle = () => send({ type: playing ? 'player_pause' : 'player_play' })
  const screen = () => {
    setCurtain(true)
    window.setTimeout(() => send({ type: 'win', action: fullscreen ? 'windowed' : 'fullscreen' }), CURTAIN_MS)
  }
  const jump = (v: number, play?: boolean, label?: { title: string; sub: string }) => {
    jumping.current = true
    setDip(true)
    clearTimeout(dipTimer.current)
    dipTimer.current = window.setTimeout(() => {
      awaiting.current = Math.min(Math.max(v, from), to)
      seek(v, play)
      if (label) {
        setCaption({ key: performance.now(), ...label })
        clearTimeout(captionTimer.current)
        captionTimer.current = window.setTimeout(() => setCaption(null), 2400)
      }
      dipTimer.current = window.setTimeout(() => {
        awaiting.current = null
        jumping.current = false
        setDip(false)
      }, 800)
    }, DIP_MS)
  }
  const frame = (d: number) => send({ type: 'player_step', d })
  const skipRound = (d: number) => {
    const rs = view.rounds
    const i = rs.reduce((n, r, k) => (r.t <= now + 0.05 ? k : n), -1)
    const r = d > 0 ? rs[i + 1] : i >= 0 && now - rs[i].t > 2 ? rs[i] : rs[i - 1]
    if (r) jump(r.t, undefined, { title: t('replayRound', { n: r.n }), sub: clock(r.t - at) })
  }
  const shown = useMemo(() => (highlight ? view.moments.filter(x => hlKinds.includes(x.kind)) : view.moments), [view, highlight, hlKinds])
  const current = shown.reduce((n, x, i) => (x.t <= now + 0.05 ? i : n), -1)
  const active = shown.reduce((n, x, i) => (x.t - PREROLL - 0.05 <= now ? i : n), -1)

  useEffect(() => {
    if (!highlight || !playing || !shown.length) return
    const prev = current >= 0 ? shown[current] : null
    const next = shown[current + 1]
    if (jumping.current || !next) return
    if ((!prev || now > prev.t + HL_AFTER) && next.t - HL_BEFORE > now + 1.5)
      jump(next.t - HL_BEFORE, false, { title: t('replayRound', { n: next.round }), sub: clock(next.t - at) })
  })

  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.tagName === 'INPUT') return
      if (e.key === ' ') {
        e.preventDefault()
        toggle()
      } else if (e.key === 'ArrowLeft' && e.shiftKey) skipRound(-1)
      else if (e.key === 'ArrowRight' && e.shiftKey) skipRound(1)
      else if (e.key === 'ArrowLeft') seek(now - 5)
      else if (e.key === 'ArrowRight') seek(now + 5)
      else if (e.key === ',') frame(-1)
      else if (e.key === '.') frame(1)
      else if (e.key === '<') setRate(v => clampRate(v - 0.25))
      else if (e.key === '>') setRate(v => clampRate(v + 0.25))
      else if (e.key === 'i' || e.key === 'I') setRange(r => [now, Math.max(now + 0.5, r?.[1] ?? now + CLIP_AFTER)])
      else if (e.key === 'o' || e.key === 'O') setRange(r => [Math.min(now - 0.5, r?.[0] ?? now - CLIP_BEFORE), now])
      else if (e.key === 'Escape' && range) setRange(null)
      else if (e.key === 'f' || e.key === 'F') screen()
      else if (e.key === 'Escape' && fullscreen) screen()
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  })

  useEffect(() => {
    if (!roundMenu) return
    const close = (e: Event) => {
      if (e instanceof KeyboardEvent ? e.key === 'Escape' : !roundBox.current?.contains(e.target as Node)) setRoundMenu(false)
    }
    window.addEventListener('pointerdown', close)
    window.addEventListener('keydown', close)
    return () => {
      window.removeEventListener('pointerdown', close)
      window.removeEventListener('keydown', close)
    }
  }, [roundMenu])

  useEffect(() => {
    const box = list.current
    const row = box?.querySelector('.rp-row.now')
    if (!box || !row) return
    const b = box.getBoundingClientRect()
    const r = row.getBoundingClientRect()
    if (r.top < b.top) box.scrollTop += r.top - b.top
    else if (r.bottom > b.bottom) box.scrollTop += r.bottom - b.bottom
  }, [active])

  const x = (v: number) => ((v - from) / span) * width
  paint.current = t => {
    const px = Math.min(width, Math.max(0, x(t)))
    if (fillEl.current) fillEl.current.style.transform = `scaleX(${width ? px / width : 0})`
    if (knobEl.current) knobEl.current.style.transform = `translateX(${px}px)`
    const text = clock(t - at)
    if (timeEl.current && timeEl.current.textContent !== text) timeEl.current.textContent = text
    const frameText = preciseClock(playing ? Math.floor(t * FRAME_TEXT_HZ) / FRAME_TEXT_HZ - at : t - at)
    if (frameEl.current && frameEl.current.textContent !== frameText) frameEl.current.textContent = frameText
    clipTicker.current?.(t)
  }
  const clusters = useMemo(() => {
    const out: { x: number; items: Moment[] }[] = []
    for (const mo of shown) {
      const px = ((mo.t - from) / span) * width
      const prev = out[out.length - 1]
      if (prev && px - prev.x < CLUSTER_PX) prev.items.push(mo)
      else out.push({ x: px, items: [mo] })
    }
    return out
  }, [shown, width, from, span])
  const heatPath = useMemo(() => {
    const h = 30
    const pts = view.heat.map((v, i) => `${(((i + 0.5) / HEAT_BINS) * width).toFixed(1)},${(h - v * (h - 3)).toFixed(1)}`)
    return `M0,${h} L${pts.join(' L')} L${width},${h} Z`
  }, [view.heat, width])

  const pick = (e: PointerEvent<HTMLElement>) => {
    const r = (track.current ?? e.currentTarget).getBoundingClientRect()
    return from + Math.min(1, Math.max(0, (e.clientX - r.left) / r.width)) * span
  }
  const clipAround = (a: number, b: number) => setRange([Math.max(from, a), Math.min(to, b)])
  const clipMoment = (i: number) => {
    const mo = shown[i]
    let end = mo.t
    for (let k = i + 1; k < shown.length && shown[k].kind === mo.kind && shown[k].t - end <= STREAK_GAP; k++) end = shown[k].t
    clipAround(mo.t - HL_BEFORE, end + HL_AFTER)
  }
  const makeClip = (preset: string, spec: ClipSpec) => {
    if (!range || !m.video) return
    const name = clipTitle(m, pair, me, range, t)
    const id = `${m.id}@${Date.now()}`
    onClip({ id, name, match: m.id, preset, state: 'making', p: 0, path: '', bytes: 0, err: '' })
    setToast(id)
    send({ type: 'clip_make', id, video: m.video, from: range[0], to: range[1], name, width: spec.width & ~1, height: spec.height, fps: spec.fps, bitrate: spec.bitrate, max_bytes: PRESETS.find(p => p.id === preset)?.max ?? 0, match: m.id, map: m.map, tag: '' })
  }

  const self = view.me
  const st = self?.stats
  const teams = pair?.roster.teams
  const mine = self && teams ? teams[self.team]?.score ?? 0 : null
  const theirs = self && teams ? teams[1 - self.team]?.score ?? 0 : null
  const result = mine === null || theirs === null ? null : mine > theirs ? 'win' : mine < theirs ? 'loss' : 'draw'
  const [steamA, steamB] = m.score.split(':').map(s => s.trim())
  const rows: ReactNode[] = []
  const byRound = new Map<number, number[]>()
  shown.forEach((mo, i) => {
    let n = 0
    for (const r of view.rounds) if (r.t <= mo.t + 0.5) n = r.n
    byRound.set(n, [...(byRound.get(n) ?? []), i])
  })
  const pushRows = (n: number) => {
    for (const i of byRound.get(n) ?? [])
      rows.push(
        <MomentRow
          key={i}
          m={shown[i]}
          avatars={avatars}
          icons={icons}
          at={at}
          now={i === active}
          cut={cuts.some(([a, b]) => shown[i].t >= a && shown[i].t <= b)}
          onClick={() => jump(shown[i].t - PREROLL, true)}
          onClip={playable ? () => clipMoment(i) : undefined}
        />,
      )
  }
  pushRows(0)
  let side = 0
  for (const r of view.rounds) {
    if (r.side && side && r.side !== side)
      rows.push(
        <div key={`s${r.n}`} className={`rp-swap${side === 3 ? ' from-ct' : ' from-t'}`}>
          <TradeIcon />
        </div>,
      )
    if (r.side) side = r.side
    rows.push(
      <div key={`r${r.n}`} className={`rp-sep${r.side === 3 ? ' ct' : r.side === 2 ? ' t' : ''}`} onClick={() => jump(r.t)}>
        <span className="rp-sep-pill">{t('replayRound', { n: r.n })}</span>
        {r.dmg !== undefined && <span className="rp-sep-dmg">{t('radarDmg', { n: r.dmg })}</span>}
        <span className="rp-sep-line" />
        {r.reason !== undefined && r.won !== 0 && <Icon name={reasonOf(r.reason).icon} ready={icons} className="rp-sep-why" alt={t(reasonOf(r.reason).label)} />}
        {r.us !== undefined && (
          <span className="rp-sep-score">
            <b className={r.won === 1 ? 'up win' : ''}>{r.us}</b>
            <i>:</i>
            <b className={r.won === -1 ? 'up loss' : ''}>{r.them}</b>
          </span>
        )}
      </div>,
    )
    pushRows(r.n)
  }
  const suggest = useMemo(() => {
    const st = view.me?.stats
    if (!st?.rounds || !pair || m.saved || !m.video) return null
    const adr = st.damage / st.rounds
    const others: number[] = []
    for (const d of demos ?? []) {
      if (d.path === pair.demo.path) continue
      const p = (rosters[d.path] ?? d.info)?.players.find(x => x.steamid === me)
      if (p?.stats?.rounds) others.push(p.stats.damage / p.stats.rounds)
    }
    if (others.length < GOOD_MIN) return null
    const share = others.filter(v => v < adr).length / others.length
    return share >= GOOD_SHARE ? { adr: Math.round(adr), share: Math.round(share * 100) } : null
  }, [view, pair, demos, rosters, me, m.saved, m.video])
  const dead = useMemo(() => (pair ? deadTime(m, pair, me, from, to) : null), [m, pair, me, from, to])
  const openSave = () => {
    setSaveOpen(true)
    if (!table) send({ type: 'replay_chunks', id: m.id })
  }
  const dismiss = () => {
    setDismissed(true)
    store(`replayDismiss:${m.id}`, '1')
  }
  const roundNow = view.rounds.reduce((n, r) => (r.t <= now + 0.05 ? r.n : n), 0)

  return (
    <div className={`rp${panel ? '' : ' nopanel'}`}>
      <div className="rp-left">
        <div
          className="rp-stage"
          ref={stage}
          onClick={e => {
            if (e.detail !== 1 || !playable) return
            clearTimeout(click.current)
            click.current = window.setTimeout(toggle, 220)
          }}
          onDoubleClick={() => {
            clearTimeout(click.current)
            screen()
          }}
        >
          {!playable && <p className="rp-msg">{m.video ? t('replayH264') : t('replayGone')}</p>}
          {playable && player?.state === 'error' && <p className="rp-msg">{t('replayError', { err: player.err })}</p>}
          {playable && !player && <p className="rp-msg faint">{t('replayLoading')}</p>}
          <div className={`rp-dip${dip ? ' on' : ''}`} />
          {toastClip && <ClipToast clip={toastClip} onOpen={onOpenClips} onClose={() => setToast(null)} />}
          {caption && (
            <div className="rp-caption" key={caption.key}>
              <strong>{caption.title}</strong>
              <span>{caption.sub}</span>
            </div>
          )}
        </div>
        <div className="rp-bar">
          {range && playable && (
            <ClipEditor
              range={range}
              setRange={setRange}
              from={from}
              to={to}
              at={at}
              playing={playing}
              ticker={clipTicker}
              sw={stretch ? Math.round(((player?.height || 720) * window.screen.width) / window.screen.height) : player?.width || 1280}
              sh={player?.height || 720}
              px={(player?.width || 1280) * (player?.height || 720)}
              onSeek={seek}
              onPause={() => send({ type: 'player_pause' })}
              onMake={(preset, spec) => {
                makeClip(preset, spec)
                setRange(null)
              }}
              onClose={() => setRange(null)}
            />
          )}
          <div className="rp-scrub">
            <div
              className="rp-track"
              ref={track}
              onPointerDown={e => {
                e.currentTarget.setPointerCapture(e.pointerId)
                seek(pick(e))
              }}
              onPointerMove={e => {
                setHover(pick(e))
                if (e.buttons & 1) seek(pick(e))
              }}
              onPointerLeave={() => {
                setHover(null)
                if (peek !== null) dropPeek()
              }}
            >
              <svg className="rp-heat" width={width} height={24} aria-hidden="true">
                <path d={heatPath} />
              </svg>
              {view.rounds.map(r => (
                <span key={r.n} className="rp-round-tick" style={{ left: x(r.t) }} />
              ))}
              <span className="rp-line" />
              {cuts.map(([a, b], i) => (
                <span key={`c${i}`} className="rp-cutspan" style={{ left: x(Math.max(a, from)), width: Math.max(0, x(Math.min(b, to)) - x(Math.max(a, from))) }} />
              ))}
              {view.rounds.map((r, i) => {
                const a = x(r.t)
                const b = x(view.rounds[i + 1]?.t ?? to)
                return <span key={`g${r.n}`} className={`rp-seg${i % 2 ? ' odd' : ''}`} style={{ left: a, width: Math.max(0, b - a - 2) }} />
              })}
              <span className="rp-fill" ref={fillEl} />
              {range && (
                <span className="rp-cliprange" style={{ left: x(range[0]), width: Math.max(2, x(range[1]) - x(range[0])) }} />
              )}
              {clusters.map((c, i) => (
                <span
                  key={i}
                  className={`rp-pin${peek === i ? ' on' : ''}`}
                  style={{ left: c.x }}
                  onPointerEnter={() => holdPeek(i)}
                  onPointerLeave={dropPeek}
                >
                  <Glyph kind={c.items[0].kind} icons={icons} />
                  {c.items.length > 1 && <em>{c.items.length}</em>}
                </span>
              ))}
              <span className="rp-knob" ref={knobEl} />
              {hover !== null && peek === null && (
                <span className="rp-hover" style={{ left: x(hover) }}>
                  {clock(hover - at)}
                </span>
              )}
            </div>
            {peek !== null && clusters[peek] && (
              <div className="rp-peek" style={{ left: Math.min(Math.max(clusters[peek].x, 150), width - 150) }} onPointerEnter={() => holdPeek(peek, true)} onPointerLeave={dropPeek}>
                {clusters[peek].items.slice(0, 5).map((mo, i) => (
                  <MomentRow
                    key={i}
                    m={mo}
                    avatars={avatars}
                    icons={icons}
                    at={at}
                    onClick={() => {
                      setPeek(null)
                      jump(mo.t - PREROLL, true)
                    }}
                  />
                ))}
              </div>
            )}
          </div>
          <div className="rp-controls">
            <button className="rp-ctl" onClick={toggle} disabled={!playable} aria-label={playing ? 'pause' : 'play'}>
              {playing ? <PauseIcon /> : <PlayIcon />}
            </button>
            <RateSlider value={rate} onChange={setRate} title={t('replaySpeed')} />
            <button className="rp-ctl" onClick={() => setMuted(v => !v)} aria-label="mute">
              {muted || volume === 0 ? <MuteIcon /> : <VolumeIcon />}
            </button>
            <input
              className="rp-volume"
              type="range"
              min={0}
              max={1}
              step={0.01}
              value={muted ? 0 : volume}
              onChange={e => {
                setMuted(false)
                setVolume(Number(e.target.value))
              }}
              style={{ ['--v' as string]: `${(muted ? 0 : volume) * 100}%` }}
            />
            <span className="rp-time">
              <span ref={timeEl} />
              <i>/ {clock(to - at)}</i>
            </span>
            <span className="rp-tools">
              <button
                className={`rp-ctl${range ? ' on' : ''}`}
                onClick={() => (range ? setRange(null) : clipAround(now - CLIP_BEFORE, now + CLIP_AFTER))}
                disabled={!playable}
                title={t('clipMode')}
              >
                <ScissorsIcon />
              </button>
              {view.rounds.length > 0 && (
                <span className="rp-rounds" ref={roundBox}>
                  <button className="rp-ctl" onClick={() => skipRound(-1)} disabled={!playable} title={t('replayPrevRound')}>
                    <SkipBackIcon />
                  </button>
                  <button className={`rp-round-now${roundMenu ? ' on' : ''}`} onClick={() => setRoundMenu(v => !v)} aria-expanded={roundMenu}>
                    {t('replayRound', { n: roundNow || view.rounds[0].n })}
                  </button>
                  {roundMenu && (
                    <span className="rp-roundmenu" role="listbox">
                      {view.rounds.map(r => (
                        <button
                          key={r.n}
                          role="option"
                          aria-selected={r.n === roundNow}
                          className={`${r.n === roundNow ? 'on' : ''}${r.side === 3 ? ' ct' : r.side === 2 ? ' t' : ''}`}
                          onClick={() => {
                            setRoundMenu(false)
                            jump(r.t, undefined, { title: t('replayRound', { n: r.n }), sub: clock(r.t - at) })
                          }}
                        >
                          <span>{t('replayRound', { n: r.n })}</span>
                          {r.dmg !== undefined && <em>{t('radarDmg', { n: r.dmg })}</em>}
                          {r.us !== undefined && (
                            <b className={r.won === 1 ? 'win' : r.won === -1 ? 'loss' : ''}>
                              {r.us} : {r.them}
                            </b>
                          )}
                        </button>
                      ))}
                    </span>
                  )}
                  <button className="rp-ctl" onClick={() => skipRound(1)} disabled={!playable} title={t('replayNextRound')}>
                    <SkipForwardIcon />
                  </button>
                </span>
              )}
              <span className="rp-frames">
                <button className="rp-ctl" onClick={() => frame(-1)} disabled={!playable} title={t('replayPrevFrame')}>
                  <StepBackIcon />
                </button>
                <span className="rp-frame-now" ref={frameEl} />
                <button className="rp-ctl" onClick={() => frame(1)} disabled={!playable} title={t('replayNextFrame')}>
                  <StepForwardIcon />
                </button>
              </span>
              {narrow && (
                <button
                  className={`rp-ctl${stretch ? ' on' : ''}`}
                  onClick={() => {
                    tweenNext.current = true
                    store('replayStretch', stretch ? '0' : '1')
                    setStretchPick(stretch ? '0' : '1')
                  }}
                  title={t('replayStretch')}
                  aria-pressed={stretch}
                >
                  <AspectIcon />
                </button>
              )}
              <button
                className={`rp-ctl${panel ? ' on' : ''}`}
                onClick={() => {
                  store('replayPanel', panel ? '0' : '1')
                  setPanel(!panel)
                }}
                title={t('replayPanel')}
                aria-pressed={panel}
              >
                <PanelIcon />
              </button>
              <button className="rp-ctl" onClick={screen} title={t('replayFull')}>
                {fullscreen ? <ShrinkIcon /> : <ExpandIcon />}
              </button>
            </span>
          </div>
        </div>
      </div>
      <aside className={`rp-right${picking ? ' picking' : ''}`}>
        <button className="rp-switcher" onClick={() => setPicking(v => !v)} aria-expanded={picking}>
          <DemosIcon />
          <span>{t('replays')}</span>
          <small>{count}</small>
          <ChevronDownIcon />
        </button>
        <div className="rp-picker" onClick={e => (e.target as HTMLElement).closest('.rp-item') && setPicking(false)}>
          {picker}
        </div>
        <div className="rp-match">
          <header className="rp-head">
            <span className="rp-head-top">
              <Face p={self} avatars={avatars} />
              <span className="rp-head-res">
                <strong className={result ?? ''}>{result ? t(result === 'win' ? 'replayWin' : result === 'loss' ? 'replayLoss' : 'replayDraw') : m.map}</strong>
                <span className="rp-head-score">
                  <b className="us">{mine ?? steamA}</b>
                  <i>:</i>
                  <b className="them">{theirs ?? steamB}</b>
                </span>
              </span>
              <span className="rp-head-map">
                <span>
                  {m.map}
                  <small>{m.mode}</small>
                </span>
                {m.video && (
                  <button
                    className={`rp-save${m.saved ? ' done' : busy ? ' busy' : ''}${saveOpen ? ' open' : ''}`}
                    onClick={() => (saveOpen ? setSaveOpen(false) : openSave())}
                    disabled={busy}
                    aria-label={m.saved ? t('replaySavedTitle') : t('replaySave')}
                    style={busy ? { ['--p' as string]: `${Math.round((saving ?? 0) * 360)}deg` } : undefined}
                  >
                    {m.saved ? <CheckIcon /> : <SaveIcon />}
                  </button>
                )}
              </span>
            </span>
            <span className="rp-stats">
              <span>
                <b>{st ? `${st.kills} / ${st.deaths} / ${st.assists}` : m.kda.replaceAll('/', ' / ')}</b>
                <small>K / D / A</small>
              </span>
              {st && (
                <>
                  <span>
                    <b>{st.kills ? `${((st.hs_kills / st.kills) * 100).toFixed(1)}%` : '0%'}</b>
                    <small>HS%</small>
                  </span>
                  <span>
                    <b>{(st.kills / Math.max(1, st.deaths)).toFixed(2)}</b>
                    <small>K/D</small>
                  </span>
                  <span>
                    <b>{Math.round(st.damage / Math.max(1, st.rounds))}</b>
                    <small>ADR</small>
                  </span>
                </>
              )}
            </span>
          </header>
          {saveOpen && m.saved && (
            <div className="rp-savebox">
              <strong>{t(m.auto ? 'replayAutoTitle' : 'replaySavedTitle')}</strong>
              <span className="rp-savebox-sum">
                <b>{mb(m.bytes ?? 0)}</b>
                {m.cuts?.length ? <span>{t('replaySavedCuts', { n: m.cuts.length })}</span> : null}
              </span>
              {m.auto && <p className="rp-savebox-note">{t('replayAutoNote', { n: keep })}</p>}
              <div className="rp-savebox-btns">
                <button className="ghost" onClick={() => setSaveOpen(false)}>
                  {t('close')}
                </button>
                {m.auto && (
                  <button
                    className="ghost"
                    onClick={() => {
                      setSaveOpen(false)
                      send({ type: 'replay_pin', id: m.id })
                    }}
                  >
                    {t('replayPin')}
                  </button>
                )}
                <button
                  className={`ghost danger${armed ? ' on' : ''}`}
                  onClick={() => {
                    if (!armed) return setArmed(true)
                    setArmed(false)
                    setSaveOpen(false)
                    send({ type: 'replay_delete', id: m.id })
                  }}
                >
                  {armed ? t('deleteConfirm') : t('replayUnsave')}
                </button>
              </div>
            </div>
          )}
          {saveOpen && !m.saved && !busy && (
            <SaveBox
              table={table}
              dead={dead}
              onClose={() => setSaveOpen(false)}
              onSave={(keep, c) => {
                setSaveOpen(false)
                onSave(keep, c)
              }}
            />
          )}
          {suggest && !dismissed && !saveOpen && saving === undefined && (
            <div className="rp-suggest">
              <span>
                <strong>{t('replaySuggest')}</strong>
                <small>{t('replaySuggestWhy', { adr: suggest.adr, n: suggest.share })}</small>
              </span>
              <button className="rp-suggest-keep" onClick={openSave}>
                {t('replayKeep')}
              </button>
              <button className="rp-ctl" onClick={dismiss} aria-label={t('close')}>
                <CloseIcon />
              </button>
            </div>
          )}
          <div className="rp-mode">
            <span>{t('replayHighlight')}</span>
            <button className={`rp-switch${highlight ? ' on' : ''}`} onClick={() => setHighlight(v => !v)} role="switch" aria-checked={highlight}>
              <span />
            </button>
            <button className={`rp-ctl${hlMenu ? ' on' : ''}`} onClick={() => setHlMenu(v => !v)} title={t('replayHlTitle')} aria-expanded={hlMenu}>
              <DotsIcon />
            </button>
          </div>
          {hlMenu && (
            <div className="rp-hlmenu">
              <strong>{t('replayHlTitle')}</strong>
              {HL_KINDS.map(k => {
                const on = hlKinds.includes(k)
                return (
                  <label key={k}>
                    <span>{t(HL_LABEL[k])}</span>
                    <button
                      className={`rp-switch${on ? ' on' : ''}`}
                      role="switch"
                      aria-checked={on}
                      onClick={() => {
                        const next = on ? hlKinds.filter(x => x !== k) : [...hlKinds, k]
                        setHlKinds(next)
                        store('replayHl', JSON.stringify(next))
                      }}
                    >
                      <span />
                    </button>
                  </label>
                )
              })}
            </div>
          )}
          {m.live && !pair && <p className="rp-nodemo">{t('replayLive')}</p>}
          {!pair && <p className="rp-nodemo">{t('replayNoDemo')}</p>}
          <div className="rp-moments" ref={list}>
            {rows}
          </div>
        </div>
      </aside>
      <div className={`rp-curtain${curtain ? ' on' : ''}`} />
    </div>
  )
}
