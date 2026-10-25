import { useCallback, useEffect, useRef, useState } from 'react'
import { Alert, Space, Spin } from 'antd'
import { getSettings } from '../api/settings'
import type { Settings } from '../api/types'
import { PlayerSettings } from './PlayerSettings'
import { NovelSettings } from './NovelSettings'
import { AuditHistorySection } from './settings/AuditHistorySection'
import { BangumiConnectionSection } from './settings/BangumiConnectionSection'
import { DownloadPathsSection } from './settings/DownloadPathsSection'
import { MikanSubscriptionsSection } from './settings/MikanSubscriptionsSection'
import { PreferencesSection } from './settings/PreferencesSection'
import { QbConnectionSection } from './settings/QbConnectionSection'
import { useAuditHistory } from './settings/useAuditHistory'
import { useBangumiConnection } from './settings/useBangumiConnection'
import { useDownloadPaths } from './settings/useDownloadPaths'
import { useMikanSubscriptions } from './settings/useMikanSubscriptions'
import { usePreferences } from './settings/usePreferences'
import { useQbConnection } from './settings/useQbConnection'
import { errorText, useRequestScope } from './settings/useRequestScope'

export function SettingsPage() {
  const [settings, setSettings] = useState<Settings | null>(null)
  const [settingsError, setSettingsError] = useState('')
  const integrationUpdates = useRef<Partial<Settings>>({})
  const initialSettings = useRequestScope()
  const mikan = useMikanSubscriptions()
  const { connectionChanged, refreshStatus, refreshFeeds } = mikan
  const updateSettings = useCallback((value: Settings) => {
    setSettings({ ...value, ...integrationUpdates.current })
  }, [])
  const qbChanged = useCallback((configured: boolean, cleared: boolean) => {
    integrationUpdates.current.qbWebUiConfigured = configured
    setSettings((previous) => previous && { ...previous, qbWebUiConfigured: configured })
    connectionChanged(configured, cleared)
  }, [connectionChanged])
  const bangumiChanged = useCallback((configured: boolean) => {
    integrationUpdates.current.bangumiConfigured = configured
    setSettings((previous) => previous && { ...previous, bangumiConfigured: configured })
  }, [])
  const qb = useQbConnection(qbChanged, mikan.clearStatusError)
  const bangumi = useBangumiConnection(bangumiChanged)
  const paths = useDownloadPaths(updateSettings)
  const preferences = usePreferences(updateSettings)
  const initializePaths = paths.initialize
  const initializePreferences = preferences.initialize

  useEffect(() => {
    const request = initialSettings.start()
    void getSettings(request.signal).then((value) => {
      if (!request.isCurrent()) return
      updateSettings(value)
      initializePaths(value)
      initializePreferences(value)
      if (value.qbWebUiConfigured) {
        void refreshStatus(true)
        void refreshFeeds()
      }
    }).catch((cause) => { if (request.isCurrent()) setSettingsError(errorText(cause)) })
    return initialSettings.invalidate
  }, [initialSettings, updateSettings, initializePaths, initializePreferences, refreshStatus, refreshFeeds])

  const audit = useAuditHistory()
  const qbPathPending = Boolean(!settings?.qbDownloadEnvironmentOverride &&
    (paths.restartRequired || settings?.qbDownloadDirectory && settings.qbDownloadDirectory !== settings.sourcePath))

  return <Space className="settings-page" direction="vertical" size="large" style={{ width: '100%' }}>
    {settingsError && <Alert type="error" message={settingsError} />}
    {paths.error && <Alert type="error" message={paths.error} />}
    {preferences.error && <Alert type="error" message={preferences.error} />}
    {!settings && !settingsError && <Spin aria-label="正在加载设置" />}
    {settings && preferences.draft && <div className="settings-grid">
      <DownloadPathsSection settings={settings} qbPathPending={qbPathPending} controller={paths} />
      <QbConnectionSection controller={qb} statusError={mikan.statusError} />
      <BangumiConnectionSection controller={bangumi} />
      <MikanSubscriptionsSection settings={settings} qbPathPending={qbPathPending} controller={mikan} />
      <PreferencesSection controller={preferences} />
      <div className="settings-section"><PlayerSettings /></div>
      <div className="settings-section wide"><NovelSettings /></div>
    </div>}
    <AuditHistorySection controller={audit} />
  </Space>
}
