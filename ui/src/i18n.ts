import { de } from './lang/de'
import { en } from './lang/en'
import { pl } from './lang/pl'

export type Key = keyof typeof en
export type Dict = Record<Key, string>

export const LANGS = [
  { id: 'en', name: 'English', game: 'english', dict: en as Dict },
  { id: 'pl', name: 'Polski', game: 'polish', dict: pl },
  { id: 'de', name: 'Deutsch', game: 'german', dict: de },
] as const

export type Lang = (typeof LANGS)[number]['id']
export type LangChoice = 'system' | Lang

const entry = (lang: Lang) => LANGS.find(l => l.id === lang) ?? LANGS[0]

export const gameLang = (lang: Lang) => entry(lang).game

export const resolve = (choice: LangChoice): Lang => {
  if (choice !== 'system') return choice
  const sys = navigator.language.toLowerCase()
  return LANGS.find(l => sys.startsWith(l.id))?.id ?? 'en'
}

export type T = (key: Key, vars?: Record<string, string | number>) => string

export const translator = (lang: Lang): T => {
  const dict = entry(lang).dict
  return (key, vars) => {
    let s = dict[key] ?? en[key]
    if (vars) for (const [k, v] of Object.entries(vars)) s = s.replace(`{${k}}`, String(v))
    return s
  }
}

export const plural = (t: T, n: number, one: Key, few: Key, many: Key) => {
  if (n === 1) return t(one)
  const f = n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14)
  return t(f ? few : many, { n })
}
