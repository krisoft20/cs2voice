import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { send, type Player as Member, type Roster } from './bridge'
import { Avatar, remember, remembered, useUi } from './common'
import { Icon, Strip } from './Details'
import { Speaker } from './icons'
import type { Playback } from './playback'
import { ENV_STEP, renderClip, type Spurt } from './voicedata'
import { teamLabels } from './voice'

type Lane = { speaker: number; member?: Member; team: number }
type View = { v0: number; v1: number }
type Window = { start: number; live: number; end: number }

const MIN_SPAN = 2
const EDGE_PX = 6
const ZOOM = 1.18
const DRAG_PX = 4
const BINS = 300
const RATES = [1, 1.25, 1.5]

export const clock = (s: number) => {
  const t = Math.max(0, Math.floor(s))
  return `${Math.floor(t / 60)}:${String(t % 60).padStart(2, '0')}`
}

const shape = (s: Spurt, v0: number, v1: number, peak: number) => {
  const env = s.env
  if (!env || !env.length) return null
  const a = Math.max(s.start, v0)
  const b = Math.min(s.end, v1)
  if (b <= a) return null
  const i0 = Math.max(0, Math.floor((a - s.start) / ENV_STEP))
  const i1 = Math.min(env.length, Math.ceil((b - s.start) / ENV_STEP))
  const n = Math.max(1, i1 - i0)
  const step = Math.max(1, Math.ceil(n / 60))
  const top: string[] = []
  const bottom: string[] = []
  for (let i = i0; i < i1; i += step) {
    let m = 0
    for (let k = i; k < Math.min(i1, i + step); k++) m = Math.max(m, env[k])
    const x = ((i - i0) / n) * 100
    const h = 46 * Math.min(1, 0.25 + (m / peak) * 0.75)
    top.push(`${x.toFixed(1)} ${(50 - h).toFixed(1)}`)
    bottom.unshift(`${x.toFixed(1)} ${(50 + h).toFixed(1)}`)
  }
  return `M0 50 L${top.join(' L')} L100 50 L${bottom.join(' L')} Z`
}

export function Voices(props: {
  roster: Roster
  demo: string
  err?: string
  avatars: Record<string, string>
  saved: string
  pb: Playback
  icons: boolean
}) {
  const { t, me } = useUi()
  const { roster, demo, err, avatars, saved, pb, icons } = props
  const { ready, failed, at, playing, skip, rate, volume, muted, toggle, togglePlay, seek, setSkip, setRate, setVolume } = pb
  const voice = pb.timeline && pb.timeline.speakers.length ? pb.timeline : null
  const [mode, setModeState] = useState<'round' | 'match'>(() => (remembered('cs2voice.voices.view', 'round') === 'match' ? 'match' : 'round'))
  const setMode = (m: 'round' | 'match') => {
    setModeState(m)
    remember('cs2voice.voices.view', m)
  }
  const [pick, setPick] = useState<number | null>(null)
  const [zoom, setZoom] = useState<View | null>(null)
  const [clip, setClip] = useState<View | null>(null)
  const [saving, setSaving] = useState(false)
  const [savedShown, setSavedShown] = useState(false)
  const savedAtMount = useRef(saved)
  const drag = useRef<{ x: number; t: number; rect: DOMRect; moved: boolean; edge: 0 | 1 | null } | null>(null)
  const [edgeHover, setEdgeHover] = useState(false)
  const lanesEl = useRef<HTMLDivElement | null>(null)
  const wheel = useRef<(e: WheelEvent) => void>(() => {})
  const onWheel = useRef((e: WheelEvent) => {
    e.preventDefault()
    wheel.current(e)
  }).current
  const lanesRef = useCallback(
    (el: HTMLDivElement | null) => {
      lanesEl.current?.removeEventListener('wheel', onWheel)
      lanesEl.current = el
      el?.addEventListener('wheel', onWheel, { passive: false })
    },
    [onWheel],
  )

  useEffect(() => {
    if (!saved || saved === savedAtMount.current) return
    setSavedShown(true)
    const timer = setTimeout(() => setSavedShown(false), 2500)
    return () => clearTimeout(timer)
  }, [saved])

  useEffect(() => {
    if (!icons) send({ type: 'icons' })
  }, [icons])

  const dt = roster.tick_interval || 1 / 64
  const windows = useMemo<Window[]>(() => {
    if (!voice) return []
    return roster.winners.map((_, r) => {
      const info = roster.round_info?.[r]
      const live = info ? info.start * dt : 0
      const vr = voice.rounds.find(v => Math.abs(v.live - live) < 2) ?? voice.rounds.find(v => live >= v.start && live < v.end)
      if (vr) return { start: vr.start, live: vr.live, end: vr.end }
      const next = roster.round_info?.[r + 1]
      return { start: Math.max(0, live - 20), live, end: next ? next.start * dt - 20 : voice.length }
    })
  }, [voice, roster, dt])

  const lanes = useMemo<Lane[]>(() => {
    if (!voice) return []
    const out: Lane[] = voice.speakers.map((id, speaker) => {
      const member = roster.players.find(p => p.steamid === id)
      return { speaker, member, team: member ? member.team : 2 }
    })
    const talk = (l: Lane) => voice.spurts.reduce((n, s) => (s.speaker === l.speaker ? n + s.end - s.start : n), 0)
    return out.sort((a, b) => a.team - b.team || talk(b) - talk(a))
  }, [voice, roster])

  const heat = useMemo(() => {
    if (!voice || !windows.length) return undefined
    const talk = windows.map(w => voice.spurts.reduce((n, s) => n + Math.max(0, Math.min(s.end, w.end) - Math.max(s.start, w.start)), 0) / Math.max(1, w.end - w.start))
    const top = Math.max(0.0001, ...talk)
    return talk.map(v => v / top)
  }, [voice, windows])

  const peak = useMemo(() => {
    if (!voice) return 1
    const all: number[] = []
    for (const s of voice.spurts) if (s.env) for (let i = 0; i < s.env.length; i += 4) all.push(s.env[i])
    if (!all.length) return 1
    all.sort((a, b) => a - b)
    return Math.max(0.0001, all[Math.floor(all.length * 0.92)])
  }, [voice, ready])

  const current = windows.findIndex(w => at >= w.start && at < w.end)
  const round = mode === 'round' && windows.length ? Math.max(0, Math.min(windows.length - 1, pick ?? (current >= 0 ? current : 0))) : null

  useEffect(() => {
    if (playing && mode === 'round' && current >= 0 && current !== pick) {
      setPick(current)
      setZoom(null)
    }
  }, [playing, current, mode])

  useEffect(() => {
    if (mode !== 'round' || !windows.length) return
    const key = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement).tagName === 'INPUT') return
      const r = round ?? 0
      const go = (n: number) => {
        setPick(n)
        setZoom(null)
        setClip(null)
        seek(windows[n].start)
      }
      if (e.key === 'ArrowLeft' && r > 0) go(r - 1)
      else if (e.key === 'ArrowRight' && r < windows.length - 1) go(r + 1)
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [mode, round, windows, seek])

  const bounds: View = voice ? (round !== null ? { v0: windows[round].start, v1: windows[round].end } : { v0: 0, v1: voice.length }) : { v0: 0, v1: 1 }
  const { v0, v1 } = zoom ?? bounds
  const span = Math.max(v1 - v0, 0.001)

  const wave = useMemo(() => {
    const out = new Float32Array(BINS)
    if (!voice) return out
    const w = span / BINS
    for (const s of voice.spurts) {
      if (muted.has(s.speaker) || s.end <= v0 || s.start >= v1) continue
      const b0 = Math.max(0, Math.floor((s.start - v0) / w))
      const b1 = Math.min(BINS - 1, Math.floor((s.end - v0) / w))
      for (let b = b0; b <= b1; b++) {
        const tm = v0 + (b + 0.5) * w
        const i = Math.floor((tm - s.start) / ENV_STEP)
        const v = s.env ? s.env[Math.max(0, Math.min(s.env.length - 1, i))] : peak * 0.5
        out[b] += v
      }
    }
    let m = 0.0001
    for (const v of out) m = Math.max(m, v)
    for (let b = 0; b < BINS; b++) out[b] /= m
    return out
  }, [voice, v0, v1, span, muted, ready, peak])

  if (roster.voice === 0) return <div className="notice">{t('noVoiceNotice')}</div>
  if (err) return <div className="notice bad">{t('voiceFailed', { err })}</div>
  if (failed) return <div className="notice bad">{t('voiceFailed', { err: failed })}</div>
  if (!voice) return <div className="notice">{t('loadingVoice')}</div>

  const x = (s: number) => ((s - v0) / span) * 100
  const inRange = (s: number, e: number) => e > v0 && s < v1
  const timeAt = (clientX: number, rect: DOMRect) => v0 + ((clientX - rect.left) / rect.width) * span
  const labels = teamLabels(roster, me, t)
  const win = round !== null ? windows[round] : null
  const playingRound = current >= 0 ? current : null

  const clampView = (a: number, b: number): View => {
    const w = Math.min(Math.max(b - a, MIN_SPAN), bounds.v1 - bounds.v0)
    let s = a
    if (s < bounds.v0) s = bounds.v0
    if (s + w > bounds.v1) s = bounds.v1 - w
    return { v0: s, v1: s + w }
  }

  wheel.current = (e: WheelEvent) => {
    const track = lanesEl.current?.querySelector('.vc-track')
    if (!track) return
    const r = track.getBoundingClientRect()
    const horizontal = e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)
    if (horizontal) {
      const d = (e.shiftKey ? e.deltaY : e.deltaX) / r.width
      setZoom(clampView(v0 + d * span, v1 + d * span))
      return
    }
    const pivot = Math.min(Math.max(timeAt(e.clientX, r), v0), v1)
    const k = e.deltaY > 0 ? ZOOM : 1 / ZOOM
    const next = clampView(pivot - (pivot - v0) * k, pivot + (v1 - pivot) * k)
    setZoom(next.v1 - next.v0 >= bounds.v1 - bounds.v0 - 0.01 ? null : next)
  }

  const edgeAt = (clientX: number, rect: DOMRect): 0 | 1 | null => {
    if (!clip) return null
    const px = (s: number) => rect.left + ((s - v0) / span) * rect.width
    if (Math.abs(clientX - px(clip.v0)) <= EDGE_PX) return 0
    if (Math.abs(clientX - px(clip.v1)) <= EDGE_PX) return 1
    return null
  }

  const onDown = (e: React.MouseEvent<HTMLDivElement>, select: boolean) => {
    if (e.button !== 0) return
    const rect = e.currentTarget.getBoundingClientRect()
    const edge = select ? edgeAt(e.clientX, rect) : null
    drag.current = { x: e.clientX, t: timeAt(e.clientX, rect), rect, moved: false, edge }
    const move = (m: MouseEvent) => {
      const d = drag.current
      if (!d) return
      if (!d.moved && d.edge === null && Math.abs(m.clientX - d.x) < DRAG_PX) return
      d.moved = true
      const tm = Math.max(0, Math.min(voice.length, timeAt(m.clientX, d.rect)))
      if (d.edge !== null && clip) {
        const other = d.edge === 0 ? clip.v1 : clip.v0
        setClip({ v0: Math.min(other, tm), v1: Math.max(other, tm) })
      } else if (select) setClip({ v0: Math.max(0, Math.min(d.t, tm)), v1: Math.min(voice.length, Math.max(d.t, tm)) })
      else seek(Math.max(v0, Math.min(v1, tm)))
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      const d = drag.current
      drag.current = null
      if (d && !d.moved && d.edge === null) {
        setClip(null)
        seek(d.t)
      }
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const saveClip = async () => {
    if (ready < 1 || !clip) return
    setSaving(true)
    try {
      const wav = await renderClip(voice, clip.v0, clip.v1, s => !muted.has(s))
      const r = windows.findIndex(w => clip.v0 >= w.start && clip.v0 < w.end)
      const base = demo.replace(/^.*[\\/]/, '').replace(/\.dem(\.zst)?$/i, '')
      const name = `${base} ${r >= 0 ? `r${r + 1} ` : ''}${clock(clip.v0).replace(':', '-')}.wav`
      send({ type: 'save_clip', name, data: wav })
    } finally {
      setSaving(false)
    }
  }

  const pickRound = (r: number) => {
    setMode('round')
    setPick(r)
    setZoom(null)
    setClip(null)
    if (windows[r]) seek(windows[r].start)
  }

  const talkIn = (speaker: number) => voice.spurts.reduce((n, s) => (s.speaker === speaker ? n + Math.max(0, Math.min(s.end, v1) - Math.max(s.start, v0)) : n), 0)
  const totalTalk = lanes.reduce((n, l) => n + talkIn(l.speaker), 0)
  const groups = [0, 1, 2].map(team => lanes.filter(l => l.team === team)).filter(g => g.length)
  const laneH = lanes.length > 10 ? Math.max(18, Math.floor(290 / lanes.length)) : 28
  const info = round !== null ? roster.round_info?.[round] : undefined
  const marks = [
    ...voice.kills.filter(k => k.at >= v0 && k.at <= v1).map(k => ({ at: k.at, icon: 'icons_ui_kill' })),
    ...(info?.bomb ?? [])
      .map(b => ({ at: b[0] * dt, icon: b[2] === 0 ? 'icons_equipment_c4' : b[2] === 1 ? 'icons_equipment_defuser' : 'icons_ui_bomb' }))
      .filter(m => m.at >= v0 && m.at <= v1),
  ]
  const deathOf = (slot: number) => voice.kills.find(k => k.victim === slot && k.at >= (win ? win.live : v0) && k.at <= v1)
  const selection = clip && inRange(clip.v0, clip.v1) && (
    <span className="vc-pick" style={{ left: `${Math.max(0, x(clip.v0))}%`, width: `${Math.min(100, x(clip.v1)) - Math.max(0, x(clip.v0))}%` }}>
      {x(clip.v0) >= 0 && <i className="start" />}
      {x(clip.v1) <= 100 && <i className="end" />}
    </span>
  )
  const title =
    playingRound !== null && (round === null || round === playingRound)
      ? at < windows[playingRound].live
        ? t('voiceFreeze', { n: playingRound + 1, t: clock(windows[playingRound].live - at) })
        : t('roundAt', { n: playingRound + 1, t: clock(at - windows[playingRound].live) })
      : round !== null
        ? t('roundN', { n: round + 1 })
        : clock(at)

  return (
    <div className="vc">
      <div className="dt-top">
        <Strip roster={roster} labels={labels} pick={round} onPick={pickRound} icons={icons} heat={heat} heatLabel={t('voiceComms')} />
        <div className="dt-views" role="tablist">
          <button role="tab" aria-selected={mode === 'round'} className={mode === 'round' ? 'on' : ''} onClick={() => setMode('round')}>
            {t('voiceRound')}
          </button>
          <button
            role="tab"
            aria-selected={mode === 'match'}
            className={mode === 'match' ? 'on' : ''}
            onClick={() => {
              setMode('match')
              setZoom(null)
            }}
          >
            {t('wholeMatch')}
          </button>
        </div>
      </div>

      <div className="vc-player">
        <button className="vc-play" onClick={togglePlay} aria-label={playing ? t('pause') : t('play')}>
          {playing ? <PauseGlyph /> : <PlayGlyph />}
        </button>
        <div className="vc-when">
          <b>{title}</b>
          <span>
            {win ? `${t('roundLong', { t: clock(win.end - win.live) })} · ` : ''}
            {t('voiceInto', { t: clock(at), all: clock(voice.length) })}
          </span>
        </div>
        <div className="vc-scrub" onMouseDown={e => onDown(e, false)}>
          <div className="vc-wave">
            {Array.from(wave, (v, b) => (
              <i key={b} className={v0 + ((b + 0.5) / BINS) * span <= at ? 'done' : ''} style={{ height: `${Math.max(6, Math.sqrt(v) * 100)}%` }} />
            ))}
          </div>
          {at >= v0 && at <= v1 && <span className="vc-now" style={{ left: `${x(at)}%` }} />}
          {marks.map((m, i) => (
            <span key={i} className="vc-mark" style={{ left: `${x(m.at)}%` }}>
              <Icon name={m.icon} ready={icons} />
            </span>
          ))}
        </div>
        <div className="vc-ctl">
          {ready < 1 && <span className="vc-prep">{t('preparing', { p: Math.round(ready * 100) })}</span>}
          <label className="vc-vol" aria-label={t('volume')}>
            <button onClick={() => setVolume(volume > 0 ? 0 : 1)} aria-label={volume > 0 ? t('mute') : t('unmute')}>
              <Speaker on={volume > 0} />
            </button>
            <input
              type="range"
              min={0}
              max={1.5}
              step={0.05}
              value={volume}
              onChange={e => setVolume(parseFloat(e.target.value))}
              style={{ ['--fill' as string]: `${(volume / 1.5) * 100}%` }}
            />
          </label>
          <button className="vc-rate" onClick={() => setRate(RATES[(RATES.indexOf(rate) + 1) % RATES.length])}>
            {rate}×
          </button>
          <button className={`vc-skip${skip ? ' on' : ''}`} onClick={() => setSkip(!skip)} aria-pressed={skip}>
            {t('skipSilence')}
          </button>
          <button className="vc-clip" onClick={saveClip} disabled={!clip || saving || ready < 1}>
            {savedShown ? t('clipSaved') : clip ? t('clipLength', { t: clock(clip.v1 - clip.v0) }) : t('clip')}
          </button>
        </div>
      </div>

      <div className="vc-lanes" ref={lanesRef} key={`${mode}${round}`} style={{ ['--lane' as string]: `${laneH}px` }}>
        {groups.map(group => {
          const team = group[0].team
          const side = team < 2 ? (round !== null ? group[0].member?.stats?.lines[round]?.[4] : roster.teams[team]?.side) === 3 ? 'ct' : 't' : 'other'
          const talking = group.filter(l => talkIn(l.speaker) > 0).length
          return (
            <div key={team} className={`vc-group ${side}`}>
              <button className="vc-ghead" onClick={() => toggle(group.map(l => l.speaker))}>
                <b>{team < 2 ? labels[team] : t('others')}</b>
                <span>{t('voiceTalking', { n: talking })}</span>
              </button>
              {group.map(l => {
                const on = !muted.has(l.speaker)
                const name = l.member?.name || t('unknownSpeaker')
                const slot = l.member?.slot ?? -1
                const talk = talkIn(l.speaker)
                const now = voice.spurts.some(s => s.speaker === l.speaker && at >= s.start && at < s.end)
                const death = slot >= 0 && mode === 'round' ? deathOf(slot) : undefined
                return (
                  <div key={l.speaker} className={`vc-lane${on ? '' : ' muted'}${now && on ? ' talk' : ''}`}>
                    <button className="vc-who" onClick={() => toggle([l.speaker])} aria-pressed={on}>
                      <Speaker on={on} />
                      <Avatar url={l.member ? avatars[l.member.steamid] : undefined} name={name} />
                      <span className="vc-nick">{name}</span>
                      <span className="vc-share">{totalTalk > 0 ? `${Math.round((100 * talk) / totalTalk)}%` : ''}</span>
                    </button>
                    <div
                      className={`vc-track${edgeHover ? ' edge' : ''}`}
                      onMouseDown={e => onDown(e, true)}
                      onMouseMove={e => {
                        const near = edgeAt(e.clientX, e.currentTarget.getBoundingClientRect()) !== null
                        if (near !== edgeHover) setEdgeHover(near)
                      }}
                      onMouseLeave={() => edgeHover && setEdgeHover(false)}
                    >
                      {death && <span className="vc-dead" style={{ left: `${Math.max(0, x(death.at))}%` }} />}
                      {voice.spurts.map((s, i) => {
                        if (s.speaker !== l.speaker || !inRange(s.start, s.end)) return null
                        const left = Math.max(0, x(s.start))
                        const width = Math.max(Math.min(100, x(s.end)) - left, 0.25)
                        const d = width > 0.6 ? shape(s, v0, v1, peak) : null
                        return (
                          <span key={i} className={`vc-cap${at >= s.start && at < s.end ? ' live' : ''}`} style={{ left: `${left}%`, width: `${width}%` }}>
                            {d && (
                              <svg viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">
                                <path d={d} />
                              </svg>
                            )}
                          </span>
                        )
                      })}
                      {death && (
                        <span className="vc-skull" style={{ left: `${x(death.at)}%` }}>
                          <Icon name="icons_ui_kill" ready={icons} />
                        </span>
                      )}
                      {selection}
                      {at >= v0 && at <= v1 && <span className="vc-line" style={{ left: `${x(at)}%` }} />}
                    </div>
                  </div>
                )
              })}
            </div>
          )
        })}
      </div>
    </div>
  )
}

const PlayGlyph = () => (
  <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
    <path d="M7 4.5v15l12-7.5z" fill="currentColor" />
  </svg>
)

const PauseGlyph = () => (
  <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
    <path d="M6.5 4.5h4v15h-4zM13.5 4.5h4v15h-4z" fill="currentColor" />
  </svg>
)
