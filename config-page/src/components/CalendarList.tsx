import React from 'react';
import { Button } from 'react-aria-components';
import { useConfig } from '../context/PebbleConfigContext';
import { CalendarEntry } from '../context/types';
import { getColorName } from '../data/colors';
import { ColorPickerModal, ORDERED_COLOR_GRID } from './ColorPicker';

const DEFAULT_COLOR = '00FF00';

function parseCalendars(raw: string): CalendarEntry[] {
  try {
    const parsed = JSON.parse(raw);
    return Array.isArray(parsed) ? parsed : [];
  } catch {
    return [];
  }
}

interface CalendarColorPickerProps {
  value: string;
  onChange: (hex: string) => void;
}

const CalendarColorPicker: React.FC<CalendarColorPickerProps> = ({ value, onChange }) => {
  const [isOpen, setIsOpen] = React.useState(false);
  const displayValue = value.toUpperCase().replace('#', '');

  return (
    <>
      <Button
        onPress={() => setIsOpen(true)}
        className="halite-color-trigger"
        aria-label="Select calendar color"
      >
        <div className="halite-color-value">
          <span className="halite-color-name">{getColorName(displayValue)}</span>
          <div className="halite-color-swatch" style={{ backgroundColor: `#${displayValue}` }} />
        </div>
      </Button>
      <ColorPickerModal
        isOpen={isOpen}
        onOpenChange={setIsOpen}
        title="Calendar color"
        value={displayValue}
        onChange={onChange}
        colorGrid={ORDERED_COLOR_GRID}
      />
    </>
  );
};

export const CalendarList: React.FC = () => {
  const { settings, updateSetting } = useConfig();
  const calendars = parseCalendars(settings.CALENDAR_CONFIG);

  const save = (next: CalendarEntry[]) => {
    updateSetting('CALENDAR_CONFIG', JSON.stringify(next));
  };

  const update = (index: number, patch: Partial<CalendarEntry>) => {
    save(calendars.map((entry, i) => (i === index ? { ...entry, ...patch } : entry)));
  };

  const remove = (index: number) => {
    save(calendars.filter((_, i) => i !== index));
  };

  const add = () => {
    save([...calendars, { url: '', color: DEFAULT_COLOR }]);
  };

  return (
    <>
      {calendars.map((entry, index) => (
        <div key={index} className="halite-item" style={{ gap: '8px' }}>
          <input
            className="halite-input"
            style={{ flex: 1, minWidth: 0 }}
            type="url"
            placeholder="https://..."
            value={entry.url}
            spellCheck={false}
            onChange={(e) => update(index, { url: e.target.value })}
          />
          <CalendarColorPicker
            value={entry.color}
            onChange={(hex) => update(index, { color: hex })}
          />
          <Button
            className="halite-custom-action-btn"
            onPress={() => remove(index)}
            aria-label="Remove calendar"
          >
            ×
          </Button>
        </div>
      ))}
      <div className="halite-item">
        <Button className="halite-custom-action-btn" onPress={add}>
          + Add calendar
        </Button>
      </div>
    </>
  );
};
