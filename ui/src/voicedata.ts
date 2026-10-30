const MAGIC = 0x56325343
const RATE = 48000
const GAP = 0.3

export type Round = { start: number; live: number; end: number }
export type Kill = { at: number; killer: number; victim: number; head: boolean }

export type Spurt = { speaker: number; start: number; end: number; frames: Uint8Array[]; pcm?: Float32Array<ArrayBuffer>; audio?: AudioBuffer; env?: Float32Array }

export const bufferOf = (s: Spurt, ctx: BaseAudioContext) => {
  if (!s.audio && s.pcm) {
    s.audio = ctx.createBuffer(1, s.pcm.length, RATE)
    s.audio.copyToChannel(s.pcm, 0)
    s.pcm = undefined
  }
  return s.audio
}

export const ENV_STEP = 0.025

export type Voice = {
  length: number
  speakers: string[]
  rounds: Round[]
  kills: Kill[]
  spurts: Spurt[]
}

const frameSeconds = (toc: number) => {
  const config = toc >> 3
  if (config < 12) return [0.01, 0.02, 0.04, 0.06][config & 3]
  if (config < 16) return [0.01, 0.02][config & 1]
  return [0.0025, 0.005, 0.01, 0.02][config & 3]
}

export const parse = (buf: ArrayBuffer): Voice | null => {
  const view = new DataView(buf)
  if (buf.byteLength < 12 || view.getUint32(0, true) !== MAGIC) return null
  const headLen = view.getUint32(8, true)
  const head = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 12, headLen))) as {
    tick_interval: number
    ticks: number
    speakers: string[]
    rounds: [number, number, number][]
    kills: [number, number, number, number][]
  }
  const dt = head.tick_interval
  const sec = (tick: number) => tick * dt
  let at = 12 + headLen
  const count = view.getUint32(at, true)
  at += 4
  const open: (Spurt | null)[] = head.speakers.map(() => null)
  const spurts: Spurt[] = []
  for (let i = 0; i < count; i++) {
    const speaker = view.getUint8(at)
    const tick = view.getInt32(at + 1, true)
    const len = view.getUint16(at + 5, true)
    const frame = new Uint8Array(buf, at + 7, len)
    at += 7 + len
    const t = sec(tick)
    let s = open[speaker]
    if (!s || t > s.end + GAP) {
      s = { speaker, start: t, end: t, frames: [] }
      open[speaker] = s
      spurts.push(s)
    }
    s.frames.push(frame)
    s.end += frameSeconds(frame[0])
  }
  return {
    length: sec(head.ticks),
    speakers: head.speakers,
    rounds: head.rounds.map(([start, live, end]) => ({ start: sec(start), live: live < 0 ? sec(start) : sec(live), end: sec(end) })),
    kills: head.kills.map(([tick, killer, victim, head]) => ({ at: sec(tick), killer, victim, head: !!head })),
    spurts,
  }
}

export type Source = { envs: Promise<void>; load: (list: Spurt[]) => Promise<void>; close: () => void }

export const open = (voice: Voice, each: (s: Spurt) => void): Source => {
  const worker = new Worker(new URL('./voicedecode.worker.ts', import.meta.url), { type: 'module' })
  const speakers = voice.speakers.map((_, i) => voice.spurts.flatMap((s, index) => (s.speaker === i ? [{ index, frames: s.frames }] : [])))
  const index = new Map(voice.spurts.map((s, i) => [s, i]))
  const asked = new Map<number, Promise<void>>()
  const waiting = new Map<number, () => void>()
  let next = 0
  let envDone = () => {}
  let envFail = (_: Error) => {}
  const envs = new Promise<void>((resolve, reject) => {
    envDone = resolve
    envFail = reject
  })
  worker.onmessage = (e: MessageEvent<{ index?: number; pcm?: Float32Array<ArrayBuffer>; env?: Float32Array<ArrayBuffer>; envs?: boolean; loaded?: number; error?: string }>) => {
    const m = e.data
    if (m.error) envFail(new Error(m.error))
    if (m.envs) envDone()
    if (m.loaded !== undefined) {
      waiting.get(m.loaded)?.()
      waiting.delete(m.loaded)
    }
    if (m.index === undefined) return
    const s = voice.spurts[m.index]
    if (m.env) {
      s.env = m.env
      each(s)
    }
    if (m.pcm) {
      if (!s.audio) s.pcm = m.pcm
      asked.delete(m.index)
    }
  }
  worker.onerror = e => envFail(new Error(e.message))
  worker.postMessage({ speakers })
  const load = (list: Spurt[]) => {
    const fresh: number[] = []
    const wait: Promise<void>[] = []
    for (const s of list) {
      const i = index.get(s)
      if (i === undefined || s.pcm || s.audio) continue
      const p = asked.get(i)
      if (p) wait.push(p)
      else fresh.push(i)
    }
    if (fresh.length) {
      const id = next++
      const p = new Promise<void>(resolve => waiting.set(id, resolve))
      for (const i of fresh) asked.set(i, p)
      wait.push(p)
      worker.postMessage({ load: id, indices: fresh })
    }
    return Promise.all(wait).then(() => {})
  }
  return { envs, load, close: () => worker.terminate() }
}

export const drop = (s: Spurt) => {
  s.pcm = undefined
  s.audio = undefined
}

const toBase64 = (bytes: Uint8Array) => {
  let out = ''
  const CHUNK = 0x8000
  for (let i = 0; i < bytes.length; i += CHUNK) out += String.fromCharCode(...bytes.subarray(i, i + CHUNK))
  return btoa(out)
}

const wav = (pcm: Float32Array) => {
  const buf = new ArrayBuffer(44 + pcm.length * 2)
  const v = new DataView(buf)
  const text = (at: number, s: string) => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)))
  text(0, 'RIFF')
  v.setUint32(4, 36 + pcm.length * 2, true)
  text(8, 'WAVE')
  text(12, 'fmt ')
  v.setUint32(16, 16, true)
  v.setUint16(20, 1, true)
  v.setUint16(22, 1, true)
  v.setUint32(24, RATE, true)
  v.setUint32(28, RATE * 2, true)
  v.setUint16(32, 2, true)
  v.setUint16(34, 16, true)
  text(36, 'data')
  v.setUint32(40, pcm.length * 2, true)
  for (let i = 0; i < pcm.length; i++) v.setInt16(44 + i * 2, Math.round(Math.max(-1, Math.min(1, pcm[i])) * 32767), true)
  return new Uint8Array(buf)
}

export const renderClip = async (voice: Voice, load: (list: Spurt[]) => Promise<void>, from: number, to: number, audible: (speaker: number) => boolean) => {
  const length = Math.max(1, Math.round((to - from) * RATE))
  const ctx = new OfflineAudioContext(1, length, RATE)
  const used = voice.spurts.filter(s => audible(s.speaker) && s.end > from && s.start < to)
  const cold = used.filter(s => !s.audio && !s.pcm)
  await load(used)
  for (const s of used) {
    if (!s.audio && !s.pcm) continue
    const src = ctx.createBufferSource()
    src.buffer = bufferOf(s, ctx)!
    src.connect(ctx.destination)
    src.start(Math.max(0, s.start - from), Math.max(0, from - s.start))
  }
  const out = await ctx.startRendering()
  for (const s of cold) drop(s)
  return toBase64(wav(out.getChannelData(0)))
}
