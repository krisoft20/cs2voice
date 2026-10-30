import type { CleanupCfg, Demo, Roster } from './bridge'

export type Reason = 'copy' | 'old' | 'broken' | 'mute'

export type CleanupRow = {
  key: string
  reason: Reason
  files: Demo[]
  bytes: number
  days: number
  roster?: Roster
  stays?: Demo
}

export const DAY = 86400

const lastUse = (d: Demo) => Math.max(d.opened ?? 0, d.mtime)

export const signature = (r: Roster | undefined) => {
  if (!r || !r.ok || r.partial || !r.players.length || !r.winners.length) return ''
  const ids = r.players.map(p => p.steamid).sort().join(',')
  return `${r.map}|${ids}|${r.winners.join('')}`
}

const keeperOf = (list: Demo[]) =>
  [...list].sort((a, b) => Number(/\.zst$/i.test(b.name)) - Number(/\.zst$/i.test(a.name)) || (b.opened ?? 0) - (a.opened ?? 0) || a.mtime - b.mtime)[0]

export const planCleanup = (demos: Demo[], rosters: Record<string, Roster>, cfg: CleanupCfg, open: string | null, now = Date.now() / 1000): CleanupRow[] => {
  const free = (d: Demo) => !d.keep && d.path !== open && !cfg.skip.includes(d.folder)
  const old = (t: number) => now - t > cfg.days * DAY
  const groups = new Map<string, Demo[]>()
  const singles: Demo[] = []
  for (const d of demos) {
    const sig = signature(rosters[d.path])
    if (!sig) singles.push(d)
    else groups.set(sig, [...(groups.get(sig) ?? []), d])
  }
  const rows: CleanupRow[] = []
  const single = (d: Demo) => {
    if (!free(d)) return
    const r = rosters[d.path]
    const days = Math.floor((now - lastUse(d)) / DAY)
    const row = { key: d.path, files: [d], bytes: d.size, days, roster: r }
    if (cfg.broken && r && !r.ok && !r.partial) rows.push({ ...row, reason: 'broken' })
    else if (old(lastUse(d))) rows.push({ ...row, reason: 'old' })
    else if (cfg.mute && r && r.voice === 0 && (!r.partial || r.voice_known)) rows.push({ ...row, reason: 'mute' })
  }
  for (const d of singles) single(d)
  for (const list of groups.values()) {
    if (list.length === 1) {
      single(list[0])
      continue
    }
    const last = Math.max(...list.map(lastUse))
    const days = Math.floor((now - last) / DAY)
    if (list.every(free) && old(last)) {
      rows.push({ key: list[0].path, reason: 'old', files: list, bytes: list.reduce((n, d) => n + d.size, 0), days, roster: rosters[list[0].path] })
      continue
    }
    if (!cfg.copies) continue
    const held = list.filter(d => !free(d))
    const stays = held.length ? held[0] : keeperOf(list)
    for (const d of list)
      if (d !== stays && free(d) && !held.includes(d))
        rows.push({ key: d.path, reason: 'copy', files: [d], bytes: d.size, days, roster: rosters[d.path], stays })
  }
  const order: Reason[] = ['copy', 'old', 'broken', 'mute']
  return rows.sort((a, b) => order.indexOf(a.reason) - order.indexOf(b.reason) || b.days - a.days)
}

export const bytesText = (bytes: number, lang: string) => {
  const mb = bytes / (1024 * 1024)
  if (mb >= 1024) return `${(mb / 1024).toLocaleString(lang, { maximumFractionDigits: 1 })} GB`
  return `${Math.round(mb).toLocaleString(lang)} MB`
}
