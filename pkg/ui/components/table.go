// Sentinel-Flow NIDS — 告警表格组件。
package components

import (
	"fmt"

	ui "github.com/gizak/termui/v3"
	"github.com/gizak/termui/v3/widgets"
)

// NewAlertTable 创建告警日志 Table 控件，根据终端宽度动态计算列宽。
func NewAlertTable(termW int) *widgets.Table {
	t := widgets.NewTable()
	t.RowSeparator = true
	t.TextAlignment = ui.AlignLeft
	t.FillRow = true
	t.Border = true
	t.BorderStyle = ui.NewStyle(BorderFg)
	t.Title = " 告警日志 "
	t.TitleStyle = ui.NewStyle(ui.ColorRed, ui.ColorClear, ui.ModifierBold)

	colWidths := ComputeColumnWidths(termW)
	t.ColumnWidths = colWidths
	t.Rows = [][]string{
		{"Sev", "Time", "Proto", "Rule", "Src -> Dst", "Description"},
	}
	return t
}

// RefreshAlertTable 根据告警切片刷新表格行数据。
func RefreshAlertTable(t *widgets.Table, alerts []AlertRecord, termW int) {
	colWidths := ComputeColumnWidths(termW)
	cDesc := colWidths[5]

	rows := make([][]string, 0, len(alerts)+2)
	rows = append(rows, []string{"Sev", "Time", "Proto", "Rule", "Src -> Dst", "Description"})

	for _, a := range alerts {
		clean := Sanitize(a.Message)
		desc := SafeTrunc(clean, cDesc)
		src := SafeTrunc(Sanitize(a.Src), 10)
		dst := SafeTrunc(Sanitize(a.Dst), 10)
		rows = append(rows, []string{
			SevShort(a.Level),
			a.Timestamp,
			a.Protocol,
			PadNum(uint64(a.RuleID), 4),
			fmt.Sprintf("%-10s > %-10s", PadRune(src, 10), PadRune(dst, 10)),
			desc,
		})
	}

	t.Rows = rows
	t.ColumnWidths = colWidths
}

// ComputeColumnWidths 根据终端宽度计算表格六列布局。
// 列定义: Sev(6) | Time(8) | Proto(6) | Rule(6) | Src→Dst(24) | Description(余量)
func ComputeColumnWidths(termW int) []int {
	innerW := termW - 2
	if innerW < 80 {
		innerW = 80
	}
	cDesc := innerW - 6 - 8 - 6 - 6 - 24
	if cDesc < 20 {
		cDesc = 30
	}
	return []int{6, 8, 6, 6, 24, cDesc}
}
