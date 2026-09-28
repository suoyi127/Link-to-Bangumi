// 分页约定：nextOffset 为 null 表示没有下一页。
export type Page<T> = { items: T[]; nextOffset: number | null }
// 明确区分 qB 与外来导入来源，以便界面和后端采用各自的处理约束。
export type InboxOrigin = 'qb_download' | 'external_import'
export type InboxPage = Page<Media> & { total: number }
export type Scan = { id: number; source: string; status: string; discoveredCount: number; processedCount: number; errorCount: number; errorSummary: string }
export type Media = { id: number; scanId: number; sourcePath: string; filename: string; parsedTitle: string; title: string; season: string; episodeNumber: string; episodeType: string; sizeBytes: number; status: string; origin: string; animeId: number | null; confidence: number; bangumiSubjectId?: number }
export type Anime = { id: number; displayTitle: string; originalTitle: string; season: string; year?: number; bangumiSubjectId?: number; coverUrl: string; locked: boolean }
export type AnimeDetail = Anime & { aliases: string[]; media: Media[]; nextMediaOffset: number | null }
export type BangumiCandidate = { id: number; name: string; nameCn: string; date: string; coverUrl: string; episodeCount: number; type: number; score: number }
export type BangumiSearch = { items: BangumiCandidate[]; autoBindEligible: boolean; fromCache: boolean; localMatch: { animeId: number; displayTitle: string; bangumiSubjectId?: number } | null }
export type Preview = { id: number; targetPath: string; operation: string; expiresAt: string; conflicts: string[] }
export type Execution = { jobId: number; targetPath: string; bytes: number; status: string }
export type Preferences = { preferredOperation: 'hardlink' | 'copy' | 'symlink'; scanIntervalSeconds: number; mpvExecutable: string; qbWebUiUrl: string }
export type Settings = Preferences & { sourcePath: string; importPath: string; libraryPath: string; dataPath: string; bangumiConfigured: boolean; qbWebUiConfigured: boolean; qbDownloadConfigured: boolean; qbDownloadDirectory: string; qbDownloadEnvironmentOverride: boolean }
export type QbStatus = { configured: boolean; connected: boolean; errorCode: string; version: string; torrentCount: number; completedCount: number }
export type QbConfig = { url: string; username: string; source: 'saved' | 'environment' | 'none'; configured: boolean }
export type QbConfigDraft = { url: string; username: string; password: string }
export type BangumiConfig = { userAgent: string; source: 'saved' | 'environment' | 'none'; configured: boolean }
export type BangumiConnectionTest = { connected: boolean; errorCode: string }
export type MetadataSync = { mikanApplied: number; bangumiBound: number; mikanErrorCode: string }
export type MikanFeeds = { feedCount: number; articleCount: number; pairCount: number; errorCode: string; feeds: { title: string; articleCount: number; hasError: boolean }[] }
export type QbAction = { success: boolean; errorCode: string }
export type QbMikanRule = { ruleName: string; feedUrl: string; keyword: string }
export type AuditLog = { id: number; action: string; entityType: string; entityId: string; createdAt: string }
