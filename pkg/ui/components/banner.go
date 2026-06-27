// Sentinel-Flow NIDS — 横幅组件。
package components

import (
	ui "github.com/gizak/termui/v3"
	"github.com/gizak/termui/v3/widgets"
)

// NewBanner 创建顶部横幅 Paragraph 控件（静态内容，无需刷新）。
func NewBanner() *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Text = JoinLines(
		QizhiLogo,
		"[ QIZHI  Security  Operations  Center ]",
		"Product: Sentinel-Flow NIDS v1.0",
		"Core:    QIZHI Lock-Free Engine v2.1",
	)
	p.Border = true
	p.BorderStyle = ui.NewStyle(BorderFg)
	p.Title = " Sentinel-Flow "
	p.TitleStyle = ui.NewStyle(ui.ColorCyan, ui.ColorClear, ui.ModifierBold)
	return p
}
