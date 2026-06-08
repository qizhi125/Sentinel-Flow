// 规则配置解析模块。
// 从 YAML 文件中加载 IDS 检测规则。
package config

import (
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

// Rule 表示一条 IDS 检测规则。
type Rule struct {
	ID          int    `yaml:"id"`          // 规则唯一标识
	Enabled     bool   `yaml:"enabled"`     // 是否启用
	Protocol    string `yaml:"protocol"`    // 协议类型（TCP/UDP/ICMP/ANY）
	Pattern     string `yaml:"pattern"`     // 匹配模式（字节序列）
	Level       int    `yaml:"level"`       // 告警等级（0-4）
	Description string `yaml:"description"` // 规则描述
}

// rulesConfig 是 YAML 文件的顶层结构。
type rulesConfig struct {
	Rules []Rule `yaml:"rules"`
}

// LoadRules 从 YAML 文件中加载规则列表。
// 仅返回 enabled=true 的规则。
func LoadRules(filepath string) ([]Rule, error) {
	data, err := os.ReadFile(filepath)
	if err != nil {
		return nil, fmt.Errorf("读取规则文件失败: %w", err)
	}

	var cfg rulesConfig
	if err := yaml.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("解析 YAML 失败: %w", err)
	}

	var active []Rule
	for _, r := range cfg.Rules {
		if r.Enabled {
			active = append(active, r)
		}
	}

	if len(active) == 0 {
		return nil, fmt.Errorf("规则文件中无启用的规则")
	}

	return active, nil
}
