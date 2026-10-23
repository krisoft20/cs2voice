import { useEffect, useRef, useState } from 'react'
import * as engine from './player'
import { decode, type Voice } from './voicedata'

export type Playback = {
  timeline: Voice | null
  ready: number
  failed: string
  at: number
  playing: boolean
  skip: boolean
  rate: number
  volume: number
  muted: Set<number>
  togglePlay: () => void
  seek: (to: number) => void
  setSkip: (on: boolean) => void
  setRate: (rate: number) => void
  setVolume: (volume: number) => void
  toggle: (speakers: number[]) => void
}

export function usePlayback(timeline: Voice | null, active: boolean): Playback {
  const [ready, setReady] = useState(0)
  const [failed, setFailed] = useState('')
  const [at, setAt] = useState(0)
  const [playing, setPlaying] = useState(false)
  const [skip, setSkipState] = useState(false)
  const [rate, setRateState] = useState(1)
  const [volume, setVolumeState] = useState(() => {
    try {
      const v = parseFloat(localStorage.getItem('cs2voice.volume') ?? '')
      return Number.isFinite(v) ? Math.max(0, Math.min(1.5, v)) : 1
    } catch {
      return 1
    }
  })
  const [muted, setMuted] = useState<Set<number>>(new Set())
  const player = useRef<engine.Player | null>(null)
  const mutedRef = useRef(muted)
  mutedRef.current = muted
  const rateRef = useRef(rate)
  rateRef.current = rate
  const volumeRef = useRef(volume)
  volumeRef.current = volume
  const skipRef = useRef(skip)
  skipRef.current = skip

  useEffect(() => {
    if (!timeline) return
    const p = engine.create(timeline)
    p.rate = rateRef.current
    p.skipSilence = skipRef.current
    p.master.gain.value = volumeRef.current
    let live = true
    setReady(timeline.spurts.length ? 0 : 1)
    setFailed('')
    setPlaying(false)
    player.current = p
    let done = 0
    let shown = 0
    if (timeline.spurts.length)
      decode(timeline, p.ctx, s => {
        if (!live) return
        engine.decoded(p, s)
        const fraction = ++done / timeline.spurts.length
        if (fraction - shown >= 0.02 || done === timeline.spurts.length) {
          shown = fraction
          setReady(fraction)
        }
      })
        .then(() => live && setReady(1))
        .catch(e => live && setFailed(String(e)))
    return () => {
      live = false
      player.current = null
      engine.destroy(p)
    }
  }, [timeline])

  useEffect(() => {
    if (!playing) return
    let frame = 0
    const step = () => {
      const p = player.current
      if (!p) return
      engine.tick(p, s => !mutedRef.current.has(s))
      setAt(engine.now(p))
      if (!p.playing) setPlaying(false)
      else frame = requestAnimationFrame(step)
    }
    frame = requestAnimationFrame(step)
    return () => cancelAnimationFrame(frame)
  }, [playing])

  const togglePlay = () => {
    const p = player.current
    if (!p || !timeline) return
    if (p.playing) {
      engine.pause(p)
      setPlaying(false)
    } else {
      engine.play(p, at >= timeline.length ? 0 : at)
      setPlaying(true)
    }
  }

  const seek = (to: number) => {
    const p = player.current
    setAt(to)
    if (p) engine.seek(p, to)
  }

  const setSkip = (on: boolean) => {
    setSkipState(on)
    if (player.current) player.current.skipSilence = on
  }

  const setVolume = (v: number) => {
    setVolumeState(v)
    if (player.current) engine.setVolume(player.current, v)
    try {
      localStorage.setItem('cs2voice.volume', String(v))
    } catch {
      return
    }
  }

  const setRate = (r: number) => {
    setRateState(r)
    if (player.current) engine.setRate(player.current, r)
  }

  const toggle = (speakers: number[]) => {
    const p = player.current
    const next = new Set(muted)
    const allOn = speakers.every(s => !muted.has(s))
    for (const s of speakers) {
      if (allOn) next.add(s)
      else next.delete(s)
      if (p) engine.setAudible(p, s, !next.has(s))
    }
    setMuted(next)
  }

  const togglePlayRef = useRef(togglePlay)
  togglePlayRef.current = togglePlay
  useEffect(() => {
    if (!active) return
    const key = (e: KeyboardEvent) => {
      if (e.code !== 'Space' || (e.target as HTMLElement).tagName === 'INPUT') return
      e.preventDefault()
      togglePlayRef.current()
    }
    window.addEventListener('keydown', key)
    return () => window.removeEventListener('keydown', key)
  }, [active])

  return { timeline, ready, failed, at, playing, skip, rate, volume, muted, togglePlay, seek, setSkip, setRate, setVolume, toggle }
}
