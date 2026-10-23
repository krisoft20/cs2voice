import type { Spurt, Voice } from './voicedata'

const LEAD = 0.05
const SKIP_AFTER = 1.5
const SKIP_BEFORE = 0.4

export type Player = {
  ctx: AudioContext
  voice: Voice
  gains: GainNode[]
  master: GainNode
  playing: boolean
  from: number
  startedAt: number
  sources: AudioBufferSourceNode[]
  skipSilence: boolean
  rate: number
}

export const create = (voice: Voice): Player => {
  const ctx = new AudioContext({ sampleRate: 48000 })
  const master = ctx.createGain()
  master.connect(ctx.destination)
  const gains = voice.speakers.map(() => {
    const g = ctx.createGain()
    g.connect(master)
    return g
  })
  return { ctx, voice, gains, master, playing: false, from: 0, startedAt: 0, sources: [], skipSilence: false, rate: 1 }
}

export const now = (p: Player) => (p.playing ? p.from + (p.ctx.currentTime - p.startedAt) * p.rate : p.from)

const stopSources = (p: Player) => {
  for (const s of p.sources) {
    s.onended = null
    try {
      s.stop()
    } catch {
      continue
    }
  }
  p.sources = []
}

const start = (p: Player, s: Spurt) => {
  const at = now(p)
  if (!s.audio || s.end <= at) return
  const src = p.ctx.createBufferSource()
  src.buffer = s.audio
  src.playbackRate.value = p.rate
  src.connect(p.gains[s.speaker])
  const when = p.startedAt + (s.start - p.from) / p.rate
  src.start(Math.max(when, p.ctx.currentTime), Math.max(0, (p.ctx.currentTime - when) * p.rate))
  p.sources.push(src)
}

const schedule = (p: Player, from: number) => {
  p.from = from
  p.startedAt = p.ctx.currentTime + LEAD
  for (const s of p.voice.spurts) start(p, s)
}

export const decoded = (p: Player, s: Spurt) => {
  if (p.playing) start(p, s)
}

export const play = (p: Player, from = now(p)) => {
  stopSources(p)
  void p.ctx.resume()
  p.playing = true
  schedule(p, Math.min(Math.max(0, from), p.voice.length))
}

export const pause = (p: Player) => {
  p.from = now(p)
  p.playing = false
  stopSources(p)
}

export const seek = (p: Player, to: number) => {
  if (p.playing) play(p, to)
  else p.from = Math.min(Math.max(0, to), p.voice.length)
}

export const setVolume = (p: Player, volume: number) => {
  p.master.gain.setTargetAtTime(volume, p.ctx.currentTime, 0.02)
}

export const setRate = (p: Player, rate: number) => {
  const at = now(p)
  p.rate = rate
  if (p.playing) play(p, at)
  else p.from = at
}

export const setAudible = (p: Player, speaker: number, on: boolean) => {
  p.gains[speaker].gain.setTargetAtTime(on ? 1 : 0, p.ctx.currentTime, 0.02)
}

export const talking = (p: Player, at: number) => p.voice.spurts.filter(s => s.start <= at && s.end > at).map(s => s.speaker)

const nextSpurt = (p: Player, at: number, audible: (speaker: number) => boolean): Spurt | null => {
  let best: Spurt | null = null
  for (const s of p.voice.spurts) if (s.start > at && audible(s.speaker) && (!best || s.start < best.start)) best = s
  return best
}

export const tick = (p: Player, audible: (speaker: number) => boolean) => {
  if (!p.playing) return
  const at = now(p)
  if (at >= p.voice.length) {
    pause(p)
    p.from = p.voice.length
    return
  }
  if (!p.skipSilence) return
  if (talking(p, at).some(audible)) return
  const next = nextSpurt(p, at, audible)
  if (next && next.start - at > SKIP_AFTER) play(p, next.start - SKIP_BEFORE)
}

export const destroy = (p: Player) => {
  stopSources(p)
  void p.ctx.close()
}
