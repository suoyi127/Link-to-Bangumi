import { useCallback, useState } from 'react'
import { putQbDownloadDirectory } from '../../api/settings'
import { requestDesktopRestart } from '../../api/desktop'
import type { Settings } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

export function useDownloadPaths(onSaved: (settings: Settings) => void) {
  const [qbPath, setQbPath] = useState('')
  const [qbPathSaving, setQbPathSaving] = useState(false)
  const [restartRequired, setRestartRequired] = useState(false)
  const [desktopRestarting, setDesktopRestarting] = useState(false)
  const [error, setError] = useState('')
  const saving = useRequestScope()
  const initialize = useCallback((settings: Settings) => setQbPath(settings.qbDownloadDirectory), [])

  async function saveQbPath() {
    if (qbPathSaving || desktopRestarting) return
    const request = saving.start()
    setQbPathSaving(true)
    setError('')
    try {
      const updated = await putQbDownloadDirectory(qbPath.trim())
      if (!request.isCurrent()) return
      onSaved(updated)
      setQbPath(updated.qbDownloadDirectory)
      setRestartRequired(updated.restartRequired)
      setDesktopRestarting(requestDesktopRestart(updated.restartRequired))
    } catch (cause) {
      if (request.isCurrent()) setError(errorText(cause))
    } finally {
      if (request.isCurrent()) setQbPathSaving(false)
    }
  }

  return { qbPath, setQbPath, qbPathSaving, restartRequired, desktopRestarting, error, initialize, saveQbPath }
}
