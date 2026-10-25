import { Alert, Button, Input, Popconfirm, Space, Typography } from 'antd'
import { SettingsSection } from './SettingsSection'
import type { useBangumiConnection } from './useBangumiConnection'

export function BangumiConnectionSection({ controller }: { controller: ReturnType<typeof useBangumiConnection> }) {
  const { bangumiConfig, bangumiAgent, setBangumiAgent, bangumiBusy, bangumiTesting, bangumiError, bangumiTestMessage, saveBangumiConnection, testBangumiConnection, clearBangumiConnection } = controller
  return <SettingsSection title="Bangumi 连接">
      <Typography.Text type="secondary">搜索、词条绑定和封面刮削直接访问 Bangumi。User-Agent 建议包含开发者 ID、应用版本和 GitHub 项目主页；目前仅用公开接口，不需要个人 Token。</Typography.Text>
      <Typography.Text>{bangumiConfig?.source === 'saved' ? '已保存 Bangumi 配置' : bangumiConfig?.source === 'environment' ? '当前使用启动环境配置' : 'Bangumi 未配置；本地浏览仍可用'}</Typography.Text>
      {bangumiError && <Alert type="error" message={bangumiError} />}
      {bangumiTestMessage && <Alert type={bangumiTestMessage.startsWith('测试成功') ? 'success' : 'warning'} message={bangumiTestMessage} />}
      <label>Bangumi User-Agent <Input aria-label="Bangumi User-Agent" value={bangumiAgent} onChange={(event) => setBangumiAgent(event.target.value)} maxLength={200} /></label>
      <Space>
        <Button loading={bangumiBusy} disabled={!bangumiAgent.trim()} onClick={() => void saveBangumiConnection()}>保存 Bangumi 配置</Button>
        <Button loading={bangumiTesting} disabled={!bangumiAgent.trim()} onClick={() => void testBangumiConnection()}>测试 Bangumi 连接</Button>
        {bangumiConfig?.source === 'saved' && <Popconfirm title="确认清除已保存的 Bangumi 配置？" description="清除后恢复启动环境配置（若有），不会删除已有封面。" okText="确认清除" cancelText="取消" onConfirm={() => void clearBangumiConnection()}><Button danger loading={bangumiBusy}>清除 Bangumi 配置</Button></Popconfirm>}
      </Space>
      </SettingsSection>
}
