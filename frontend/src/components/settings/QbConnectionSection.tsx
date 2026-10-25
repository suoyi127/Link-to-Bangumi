import { Alert, Button, Input, Popconfirm, Space, Typography } from 'antd'
import { SettingsSection } from './SettingsSection'
import type { useQbConnection } from './useQbConnection'

export function QbConnectionSection({ controller, statusError }: { controller: ReturnType<typeof useQbConnection>; statusError: string }) {
  const { qbConfig, qbDraft, setQbDraft, qbConfigBusy, qbTestBusy, qbTestMessage, saveQbConnection, testQbConnection, clearQbConnection } = controller
  const qbConfigError = controller.qbConfigError || statusError
  return <SettingsSection title="qB Web UI 连接">
      <Typography.Text type="secondary">仅支持本机回环地址，例如 http://[::1]:8080。密码保存在当前 Windows 用户的凭据管理器中，不会回显；已保存配置时留空可保持原密码。</Typography.Text>
      {qbConfig?.source === 'saved' ? <Typography.Text>已保存 qB Web UI 配置</Typography.Text> : qbConfig?.source === 'environment' ? <Typography.Text>当前使用启动环境配置</Typography.Text> : <Typography.Text>尚未配置 qB Web UI</Typography.Text>}
      {qbConfigError && <Alert type="error" message={qbConfigError} />}
      {qbTestMessage && <Alert type={qbTestMessage.startsWith('测试成功') ? 'success' : 'warning'} message={qbTestMessage} />}
      <label>qB Web UI 地址 <Input aria-label="qB Web UI 地址" value={qbDraft.url} onChange={(event) => setQbDraft({ ...qbDraft, url: event.target.value })} maxLength={64} placeholder="http://[::1]:8080" /></label>
      <label>qB Web UI 用户名 <Input aria-label="qB Web UI 用户名" value={qbDraft.username} onChange={(event) => setQbDraft({ ...qbDraft, username: event.target.value })} maxLength={128} /></label>
      <label>qB Web UI 密码 <Input.Password aria-label="qB Web UI 密码" value={qbDraft.password} onChange={(event) => setQbDraft({ ...qbDraft, password: event.target.value })} maxLength={512} autoComplete="new-password" /></label>
      <Space>
        <Button loading={qbConfigBusy} disabled={!qbDraft.url || !qbDraft.username || (!qbDraft.password && qbConfig?.source !== 'saved')} onClick={() => void saveQbConnection()}>保存 qB 连接</Button>
        <Button loading={qbTestBusy} disabled={!qbDraft.url || !qbDraft.username || (!qbDraft.password && qbConfig?.source !== 'saved')} onClick={() => void testQbConnection()}>测试 qB 连接</Button>
        {qbConfig?.source === 'saved' && <Popconfirm title="确认清除已保存的 qB Web UI 配置？" description="清除后会恢复启动环境配置（若有），不会更改 qB 的下载任务。" okText="确认清除" cancelText="取消" onConfirm={() => void clearQbConnection()}><Button danger loading={qbConfigBusy}>清除 qB 配置</Button></Popconfirm>}
      </Space>
      </SettingsSection>
}
