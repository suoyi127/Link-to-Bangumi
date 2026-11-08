import { json, request } from './request'

export type SteamConfig = { steamPath: string; steamId: string; hasApiKey: boolean }
export const getSteamConfig = (signal?: AbortSignal) => request<SteamConfig>('/api/steam/config', { signal })
export const saveSteamConfig = (config: { steamPath: string; steamId: string; apiKey: string }) => request<SteamConfig>('/api/steam/config', { ...json(config), method: 'PUT' })
export const importSteamGames = () => request<{ imported: number; updated: number; warnings: string[] }>('/api/steam/imports', json({}))
export type SteamLoginStatus = { state: 'pending' | 'completed' | 'cancelled' | 'expired' | 'failed'; errorCode: string }
export const startSteamLogin = (signal?: AbortSignal) => request<{ sessionId: string; state: 'pending' }>('/api/steam/login', { ...json({}), signal })
export const getSteamLogin = (sessionId: string, signal?: AbortSignal) => request<SteamLoginStatus>(`/api/steam/login?sessionId=${encodeURIComponent(sessionId)}`, { signal })
