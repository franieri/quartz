#!/bin/bash
# =============================================================================
# Compare Benchmark Results
# =============================================================================
# Compares two benchmark runs and shows performance differences
#
# Usage:
#   ./compare_results.sh results/baseline results/latest
# =============================================================================

set -e

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <baseline_dir> <compare_dir>"
    echo ""
    echo "Example:"
    echo "  $0 results/2026-01-01_12-00-00 results/latest"
    exit 1
fi

BASELINE_DIR="$1"
COMPARE_DIR="$2"

# Resolve symlinks
[[ -L "$BASELINE_DIR" ]] && BASELINE_DIR=$(readlink -f "$BASELINE_DIR")
[[ -L "$COMPARE_DIR" ]] && COMPARE_DIR=$(readlink -f "$COMPARE_DIR")

BASELINE_CSV="$BASELINE_DIR/results.csv"
COMPARE_CSV="$COMPARE_DIR/results.csv"

if [[ ! -f "$BASELINE_CSV" ]]; then
    echo "Error: Baseline results not found: $BASELINE_CSV"
    exit 1
fi

if [[ ! -f "$COMPARE_CSV" ]]; then
    echo "Error: Compare results not found: $COMPARE_CSV"
    exit 1
fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=============================================="
echo "   Benchmark Comparison Report"
echo "=============================================="
echo -e "${NC}"
echo "Baseline: $(basename "$BASELINE_DIR")"
echo "Compare:  $(basename "$COMPARE_DIR")"
echo ""

echo -e "${CYAN}--- Performance Changes ---${NC}"
echo ""
printf "%-45s %10s %10s %12s\n" "Benchmark" "Baseline" "Current" "Change"
printf "%-45s %10s %10s %12s\n" "---------" "--------" "-------" "------"

# Process results
improved=0
regressed=0
unchanged=0

while IFS=, read -r cat name avg_ms std min max runs; do
    [[ "$cat" == "category" ]] && continue  # Skip header
    
    # Find matching baseline
    baseline=$(grep ",$name," "$BASELINE_CSV" 2>/dev/null | head -1 | cut -d, -f3)
    
    if [[ -n "$baseline" && "$baseline" != "0" ]]; then
        # Calculate percentage change
        change=$(echo "scale=1; (($avg_ms - $baseline) / $baseline) * 100" | bc 2>/dev/null || echo "0")
        
        # Determine color and direction
        if (( $(echo "$change < -5" | bc -l 2>/dev/null || echo 0) )); then
            color=$GREEN
            symbol="↓"
            ((improved++))
        elif (( $(echo "$change > 5" | bc -l 2>/dev/null || echo 0) )); then
            color=$RED
            symbol="↑"
            ((regressed++))
        else
            color=$NC
            symbol="="
            ((unchanged++))
        fi
        
        printf "%-45s %8d ms %8d ms ${color}%+6.1f%% %s${NC}\n" "$name" "$baseline" "$avg_ms" "$change" "$symbol"
    else
        printf "%-45s %10s %8d ms %12s\n" "$name" "N/A" "$avg_ms" "(new)"
    fi
done < "$COMPARE_CSV"

echo ""
echo -e "${CYAN}--- Summary ---${NC}"
echo ""
echo -e "${GREEN}Improved (>5% faster): $improved${NC}"
echo -e "${RED}Regressed (>5% slower): $regressed${NC}"
echo "Unchanged: $unchanged"
echo ""

# Overall change
baseline_total=$(tail -n +2 "$BASELINE_CSV" | awk -F, '{sum+=$3} END {print sum}')
compare_total=$(tail -n +2 "$COMPARE_CSV" | awk -F, '{sum+=$3} END {print sum}')

if [[ -n "$baseline_total" && "$baseline_total" != "0" ]]; then
    overall_change=$(echo "scale=1; (($compare_total - $baseline_total) / $baseline_total) * 100" | bc 2>/dev/null || echo "0")
    
    if (( $(echo "$overall_change < 0" | bc -l 2>/dev/null || echo 0) )); then
        echo -e "Overall: ${GREEN}${overall_change}% faster${NC}"
    else
        echo -e "Overall: ${RED}+${overall_change}% slower${NC}"
    fi
fi
