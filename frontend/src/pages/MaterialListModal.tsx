import React, { useEffect, useMemo, useState } from 'react';
import {
  formatMaterialDisplayName,
  formatMaterialSummary,
  type MaterialsControls,
} from '../application/useMaterials';
import type { MenuLabels } from '../platform/bridge';
import { NativeScrollView } from '../platform/native';
import { LHoloButton, LHoloTextField, LHoloCheckbox } from '../components/LHoloControls';
import { LHoloSelect } from '../components/LHoloSelect';
import { LHoloItemIcon } from '../components/LHoloItemIcon';
import { SelectedFileContext } from '../components/SelectedFileContext';

export function MaterialListModal({
  labels,
  hasLoadedStructure,
  sourceName,
  materialsState,
}: {
  labels: MenuLabels;
  hasLoadedStructure: boolean;
  sourceName: string;
  materialsState: MaterialsControls;
}) {
  const { materials, ready, closeModal } = materialsState;
  const [query, setQuery] = useState('');
  const [sort, setSort] = useState(0);
  const [stacks, setStacks] = useState(false);
  const [unavailableOnly, setUnavailableOnly] = useState(false);
  useEffect(() => { setQuery(''); }, [sourceName, materialsState.isOpen]);
  const visibleMaterials = useMemo(() => {
    const term = query.trim().toLowerCase();
    const items = materials.filter(item => (!term
      || formatMaterialDisplayName(item, labels).toLowerCase().includes(term)
      || item.typeName.toLowerCase().includes(term))
      && (!unavailableOnly || item.available === undefined || item.inventoryCountable === false || item.available < item.count));
    if (sort !== 0) items.sort((a, b) => {
      const names = formatMaterialDisplayName(a, labels).localeCompare(formatMaterialDisplayName(b, labels))
        || a.typeName.localeCompare(b.typeName);
      return sort === 1 ? names : (sort === 2 ? b.count - a.count : a.count - b.count) || names;
    });
    return items;
  }, [materials, labels, query, sort, unavailableOnly]);
  if (!materialsState.isOpen) return null;

  return <div className="lholo-modal-backdrop">
    <div className="lholo-material-modal">
      <div className="lholo-material-modal-header">
        <div className="lholo-material-title">{labels['material.listTitle']}</div>
        <LHoloButton label={labels['button.close']} onClick={closeModal} />
      </div>
      {sourceName ? <SelectedFileContext label={labels['tasks.currentSelection']} name={sourceName} /> : null}
      {!hasLoadedStructure ? (
        <div className="lholo-muted lholo-material-empty">{labels['status.notLoaded']}</div>
      ) : !ready ? (
        <div className="lholo-muted lholo-material-empty">{labels['material.loading']}</div>
      ) : (
        <>
          <div className="lholo-material-summary">
            {formatMaterialSummary(materials, labels)}
          </div>
          <div className="lholo-material-tools">
            <LHoloTextField label={labels['material.search']} placeholder={labels['material.search']}
              value={query} onChange={setQuery} maxLength={256} />
            <LHoloSelect label={labels['material.sort']} value={sort} onChange={setSort}
              options={[labels['material.sortDefault'], labels['material.sortName'],
                labels['material.sortMost'], labels['material.sortLeast']]} />
            <LHoloCheckbox label={labels['material.stacks']} value={stacks} onChange={setStacks} />
            <LHoloCheckbox label={labels['material.unavailableOnly']} value={unavailableOnly} onChange={setUnavailableOnly} />
          </div>
          {visibleMaterials.length === 0 ? (
            <div className="lholo-muted lholo-material-empty">
              {labels[materials.length === 0 ? 'hint.noPlaceableMaterials' : 'material.noMatches']}
            </div>
          ) : (
            <div className="lholo-material-table">
              <div className="lholo-material-table-header">
                <div className="lholo-material-col-item">{labels['column.item']}</div>
                <div className="lholo-material-col-id">{labels['column.identifier']}</div>
                <div className="lholo-material-col-available">{labels['material.available']}</div>
                <div className="lholo-material-col-total">{labels['column.total']}</div>
              </div>
              <div className="lholo-material-table-viewport">
                <NativeScrollView>
                  <div className="lholo-material-table-body">
                    {visibleMaterials.map((item, index) => (
                      <div key={item.typeName + ':' + index} className="lholo-material-table-row">
                        <div className="lholo-material-col-item">
                          <LHoloItemIcon iconId={item.iconId} iconData={item.iconData} />
                          <div className="lholo-material-name">{formatMaterialDisplayName(item, labels)}</div>
                        </div>
                        <div className="lholo-material-col-id">{item.typeName}</div>
                        <div className="lholo-material-col-available">{item.inventoryCountable === false || item.available === undefined ? '—' : String(item.available)}</div>
                        <div className="lholo-material-col-total">{stacks && item.stackSize && item.stackSize > 1
                          ? `${Math.floor(item.count / item.stackSize)} × ${item.stackSize} + ${item.count % item.stackSize}` : String(item.count)}</div>
                      </div>
                    ))}
                  </div>
                </NativeScrollView>
              </div>
            </div>
          )}
        </>
      )}
    </div>
  </div>;
}
