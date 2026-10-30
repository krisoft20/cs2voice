export type Team = { name: string; score: number; side: number }

export type Stats = {
  kills: number
  deaths: number
  assists: number
  damage: number
  hs_kills: number
  wallbang_kills: number
  smoke_kills: number
  rounds: number
  kast: number
  shots: number
  hits: number
  head_hits: number
  entry_kills: number
  entry_deaths: number
  trade_kills: number
  clutch_rounds: number
  clutches: number
  k2: number
  k3: number
  k4: number
  k5: number
  utility_damage: number
  enemies_flashed: number
  flash_assists: number
  ping?: number
  flashes: number
  enemy_blinds: number
  enemy_blind_ms: number
  team_blind_ms: number
  lines: [number, number, number, number, number][]
  eco?: [number, number, number, number, number][]
}

export type PerfRow = {
  steamid: string
  ttd: number | null
  ttd_n: number
  reaction: number | null
  reaction_n: number
  placement: number | null
  placement_n: number
  fps?: number
  sens?: number
  first: number
  first_hits: number
  stops: number
  stopped: number
  swings: number
  swings_good: number
}

export type SkinItem = {
  weapon: string
  skin: string
  rarity: string
  color: string
  rank: number
  star: boolean
  gloves: boolean
  slot: string
  sides: number
  paint: number
  wear: number
  wear_name: string
  seed: number
  st: number
  tag: string
  img: string
  variants: { icon: string; img: string }[]
  stickers: { name: string; img: string }[]
}

export type SkinsMsg = { type: 'skins'; path: string; err: string; players: { steamid: string; music: string; items: SkinItem[] }[] }

export type PerfMsg = { type: 'perf'; path: string; err: string; players: PerfRow[] }

export type Profile = { avatar: string; vac: boolean; limited: boolean; hidden: boolean; since: number }

export type Player = { slot: number; name: string; crosshair?: string; steamid: string; team: number; voice: number; mute?: number; stats?: Stats; demoName?: string; level?: number; elo?: number; rank?: number }

export type FaceitPlayer = { steamid: string; nick: string; level: number; elo: number; rank: number }

export type Roster = {
  type: 'roster'
  stale?: boolean
  path: string
  ok: boolean
  partial: boolean
  ended?: boolean
  voice_known?: boolean
  pet_crash?: boolean
  err: string
  map: string
  server: string
  voice: number
  duration: number
  rounds: number
  teams: Team[]
  players: Player[]
  winners: number[]
  round_info?: RoundInfo[]
  tick_interval?: number
}

export type RoundInfo = {
  start: number
  decided: number
  reason: number
  kills: [number, number, number, number, number, string][]
  bomb: [number, number, number][]
}

export type HistoryMatch = { key: string; date: number; path: string; map: string; score: [number, number]; players: [string, string, number, number, number, number][] }

export type Demo = { path: string; name: string; folder: string; size: number; mtime: number; opened?: number; keep?: boolean; info?: Roster }

export type SetupInfo = { store: boolean; installed: boolean; here: boolean; desktop: boolean; autostart: boolean; dir: string }

export type CleanupCfg = {
  mode: 'off' | 'ask' | 'auto'
  method: 'recycle' | 'delete'
  days: number
  copies: boolean
  broken: boolean
  mute: boolean
  snooze: number
  skip: string[]
}

export type Folder = { path: string; custom: boolean }

export type Storage = { key: string; bytes: number; files: number }

export type ReplayEvent = [number, string, string, string, string]

export type ReplayMatch = {
  id: string
  mode: string
  map: string
  kda: string
  score: string
  unix: number
  duration: number
  video?: string
  codec?: string
  at?: number
  from?: number
  to?: number
  saved?: boolean
  auto?: boolean
  live?: boolean
  done?: boolean
  bytes?: number
  cuts?: [number, number][]
  events: ReplayEvent[]
}

export type ClipMeta = { match: string; map: string; tag: string; from: number; to: number; len: number; fav: boolean }

export type ClipFile = { file: string; path: string; bytes: number; mtime: number; thumb: boolean; meta: ClipMeta | null }

export type Clip = {
  id: string
  name: string
  match: string
  preset: string
  state: 'making' | 'done' | 'failed'
  p: number
  path: string
  bytes: number
  err: string
}

export type PlayerState = {
  state: 'playing' | 'paused' | 'ended' | 'error' | 'closed'
  t: number
  rate?: number
  start: number
  end: number
  width: number
  height: number
  err: string
}

export type Incoming =
  | { type: 'demos'; items: Demo[] }
  | { type: 'folders'; items: Folder[] }
  | Roster
  | { type: 'reading'; path: string }
  | { type: 'copied'; text: string }
  | { type: 'profiles'; items: Record<string, Profile> }
  | { type: 'me'; steamid: string }
  | { type: 'history'; matches: HistoryMatch[] }
  | { type: 'history_add'; match: HistoryMatch }
  | { type: 'art'; map: string }
  | { type: 'settings'; version: string; lang: string; loader?: string; updates: boolean; close?: string; steam?: boolean; talk?: boolean; talkStyle?: number; talkLead?: number; talkMin?: number; talkRefresh?: number; setup?: SetupInfo; cleanup?: CleanupCfg }
  | { type: 'close_ask' }
  | { type: 'setup_error'; err: string }
  | { type: 'update'; version: string; notes: string; size: number; store?: boolean }
  | { type: 'update_state'; state: 'downloading' | 'failed'; done?: number; total?: number; err?: string }
  | { type: 'voice'; path: string; err: string; data?: ArrayBuffer }
  | { type: 'track'; path: string; err: string; data?: ArrayBuffer }
  | PerfMsg
  | SkinsMsg
  | { type: 'econ_img'; img: string }
  | { type: 'icons' }
  | { type: 'radar'; map: string; ok: boolean; pos_x: number; pos_y: number; scale: number; lower: boolean; split_z: number }
  | { type: 'saved'; path: string }
  | { type: 'storage'; items: Storage[] }
  | { type: 'cleared' }
  | { type: 'cache_cleared' }
  | { type: 'window'; maximized: boolean; fullscreen?: boolean }
  | { type: 'watch'; state: 'unpacking' | 'copied' | 'error'; err?: string; line?: string }
  | { type: 'fixed'; path: string; ok: boolean; err: string }
  | { type: 'faceit'; path: string; players: FaceitPlayer[] }
  | { type: 'replays'; folder: string; background: number; keep: number; rate?: number; buffer?: number; length?: number; free?: number; saved_dir?: string; matches: ReplayMatch[] }
  | { type: 'replay_saving'; id: string; done: number; total: number }
  | { type: 'replay_saved'; id: string; ok: boolean }
  | { type: 'replay_chunks'; id: string; list: [number, number, number, number][] }
  | { type: 'clip_progress'; id: string; p: number }
  | { type: 'clip_done'; id: string; ok: boolean; path: string; bytes: number; err: string }
  | { type: 'clips'; dir: string; bytes: number; items: ClipFile[] }
  | { type: 'clip_thumb'; file: string; len: number }
  | ({ type: 'player' } & PlayerState)

type Outgoing =
  | { type: 'ready' }
  | { type: 'open'; path: string }
  | { type: 'copy'; text: string }
  | { type: 'update_install' }
  | { type: 'keep'; paths: string[]; on: boolean }
  | { type: 'cleanup'; paths: string[]; method?: 'recycle' | 'delete' }
  | { type: 'cleanup_folder'; path: string; on: boolean }
  | { type: 'rescan' }
  | { type: 'add_folder' }
  | { type: 'remove_folder'; path: string }
  | { type: 'reveal'; path: string }
  | { type: 'profiles'; ids: string[] }
  | { type: 'steam'; id: string }
  | { type: 'faceit_profile'; nick: string }
  | { type: 'art'; map: string }
  | { type: 'voice'; path: string }
  | { type: 'track'; path: string }
  | { type: 'radar'; map: string }
  | { type: 'skins'; path: string; lang: string }
  | { type: 'econ_want'; icons: string[] }
  | { type: 'icons' }
  | { type: 'save_clip'; name: string; data: string }
  | { type: 'storage' }
  | { type: 'clear_unpacked' }
  | { type: 'clear_cache' }
  | { type: 'win'; action: 'minimize' | 'maximize' | 'close' | 'tray' | 'quit' | 'stay' | 'fullscreen' | 'windowed' }
  | { type: 'tray_text'; open: string; quit: string }
  | { type: 'install'; desktop: boolean; autostart: boolean }
  | { type: 'uninstall'; data: boolean }
  | { type: 'open_url'; url: string }
  | { type: 'delete_demos'; paths: string[] }
  | { type: 'move_demos'; paths: string[] }
  | { type: 'copy_paths'; paths: string[] }
  | { type: 'watch'; path: string; fix: boolean; low: number; high: number }
  | { type: 'set'; key: string; value: string }
  | { type: 'fix_demo'; path: string }
  | { type: 'faceit'; path: string }
  | { type: 'replays' }
  | { type: 'replay_save'; id: string; keep: number[]; cuts: [number, number][] }
  | { type: 'replay_chunks'; id: string }
  | { type: 'replay_delete'; id: string }
  | { type: 'replay_pin'; id: string }
  | { type: 'replay_keep'; n: number }
  | { type: 'clip_make'; id: string; video: string; from: number; to: number; name: string; width: number; height: number; fps: number; bitrate: number; max_bytes: number; match: string; map: string; tag: string }
  | { type: 'clip_cancel'; id: string }
  | { type: 'clip_copy'; paths: string[] }
  | { type: 'clip_show'; path: string }
  | { type: 'clips' }
  | { type: 'clip_delete'; paths: string[] }
  | { type: 'clip_rename'; path: string; name: string }
  | { type: 'clip_fav'; path: string; on: boolean }
  | { type: 'player_open'; video: string; t: number }
  | { type: 'player_play' }
  | { type: 'player_pause' }
  | { type: 'player_seek'; t: number }
  | { type: 'player_close' }
  | { type: 'player_rate'; v: number }
  | { type: 'player_step'; d: number }
  | { type: 'player_volume'; v: number }
  | { type: 'player_rect'; x: number; y: number; w: number; h: number }

type SharedBufferEvent = { getBuffer: () => ArrayBuffer; additionalData: unknown }

type WebView = {
  postMessage: (m: unknown) => void
  addEventListener: {
    (t: 'message', fn: (e: { data: unknown }) => void): void
    (t: 'sharedbufferreceived', fn: (e: SharedBufferEvent) => void): void
  }
  releaseBuffer: (b: ArrayBuffer) => void
}

const webview = (window as unknown as { chrome?: { webview?: WebView } }).chrome?.webview

export const send = (m: Outgoing) => {
  if (webview) webview.postMessage(m)
  else void import('./mock').then(mod => mod.handle(m, deliver))
}

let listener: ((m: Incoming) => void) | null = null

const deliver = (m: Incoming) => listener?.(m)

export const listen = (fn: (m: Incoming) => void) => {
  listener = fn
  webview?.addEventListener('message', e => deliver(e.data as Incoming))
  webview?.addEventListener('sharedbufferreceived', e => {
    const shared = e.getBuffer()
    const data = shared.slice(0)
    webview.releaseBuffer(shared)
    deliver({ ...(e.additionalData as { type: 'voice' | 'track'; path: string; err: string }), data })
  })
}
