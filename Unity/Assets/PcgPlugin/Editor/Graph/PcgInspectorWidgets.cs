using System;
using System.Globalization;
using UnityEngine;
using UnityEngine.UIElements;

namespace DJTechEditor.PCG.Graph
{
    /// <summary>Shared slider + numeric field row (matches PcgGraphComponentEditor Parameters UI).</summary>
    internal static class PcgInspectorWidgets
    {
        private const float ControlHeight = 18f;

        public static VisualElement CreateSliderRow(
            bool isInteger,
            float min,
            float max,
            float currentValue,
            Action<float> onSliderChange,
            string label = null,
            Action onDragBegin = null,
            Action onDragEnd = null,
            Action<float> onFieldCommit = null,
            bool readOnly = false,
            bool numericFirst = false)
        {
            onFieldCommit ??= onSliderChange;
            var container = new VisualElement();
            if (!string.IsNullOrEmpty(label))
            {
                container.Add(new Label(label)
                {
                    style = { color = new Color(0.75f, 0.75f, 0.75f), fontSize = 10, marginBottom = 2 },
                });
            }

            var clamped = Mathf.Clamp(currentValue, min, max);
            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    minHeight = ControlHeight,
                    width = Length.Percent(100),
                },
            };

            var slider = new Slider(min, max) { value = clamped };
            ConfigureCompactSlider(slider);
            slider.SetEnabled(!readOnly);

            if (isInteger)
            {
                var intField = new IntegerField { value = Mathf.RoundToInt(clamped) };
                ConfigureCompactNumericField(intField);
                intField.SetEnabled(!readOnly);

                slider.RegisterCallback<PointerDownEvent>(_ => onDragBegin?.Invoke());
                slider.RegisterValueChangedCallback(evt =>
                {
                    var v = Mathf.RoundToInt(Mathf.Clamp(evt.newValue, min, max));
                    intField.SetValueWithoutNotify(v);
                    onSliderChange(v);
                });
                slider.RegisterCallback<PointerUpEvent>(_ => onDragEnd?.Invoke());

                intField.RegisterValueChangedCallback(evt =>
                {
                    var v = Mathf.Clamp(evt.newValue, Mathf.RoundToInt(min), Mathf.RoundToInt(max));
                    slider.SetValueWithoutNotify(v);
                    onFieldCommit(v);
                });

                if (numericFirst)
                {
                    intField.style.marginLeft = 0;
                    intField.style.marginRight = 4;
                    row.Add(intField);
                    row.Add(slider);
                }
                else
                {
                    row.Add(slider);
                    row.Add(intField);
                }
            }
            else
            {
                var floatField = new FloatField { value = clamped };
                ConfigureCompactNumericField(floatField);
                floatField.SetEnabled(!readOnly);

                slider.RegisterCallback<PointerDownEvent>(_ => onDragBegin?.Invoke());
                slider.RegisterValueChangedCallback(evt =>
                {
                    var v = Mathf.Clamp(evt.newValue, min, max);
                    floatField.SetValueWithoutNotify(v);
                    onSliderChange(v);
                });
                slider.RegisterCallback<PointerUpEvent>(_ => onDragEnd?.Invoke());

                floatField.RegisterValueChangedCallback(evt =>
                {
                    var v = Mathf.Clamp(evt.newValue, min, max);
                    slider.SetValueWithoutNotify(v);
                    onFieldCommit(v);
                });

                if (numericFirst)
                {
                    floatField.style.marginLeft = 0;
                    floatField.style.marginRight = 4;
                    row.Add(floatField);
                    row.Add(slider);
                }
                else
                {
                    row.Add(slider);
                    row.Add(floatField);
                }
            }

            container.Add(row);
            return container;
        }

        private static void ConfigureCompactSlider(Slider slider)
        {
            slider.label = string.Empty;
            slider.AddToClassList(BaseField<float>.noLabelVariantUssClassName);
            slider.style.flexGrow = 1;
            slider.style.flexShrink = 1;
            slider.style.minWidth = 48;
            slider.style.height = ControlHeight;
            slider.style.marginTop = 0;
            slider.style.marginBottom = 0;
            ConfigureInputArea(slider, Slider.inputUssClassName);
        }

        public static void ConfigureCompactNumericField<T>(BaseField<T> field)
        {
            field.label = string.Empty;
            field.AddToClassList(BaseField<T>.noLabelVariantUssClassName);
            field.style.width = 50;
            field.style.minWidth = 50;
            field.style.height = ControlHeight;
            field.style.flexGrow = 0;
            field.style.flexShrink = 0;
            field.style.marginLeft = 4;
            field.style.marginTop = 0;
            field.style.marginBottom = 0;
            ConfigureInputArea(field, BaseField<T>.inputUssClassName);
        }

        private static void ConfigureInputArea(VisualElement root, string inputClassName)
        {
            var input = root.Q(className: inputClassName);
            if (input == null)
                return;

            input.style.height = ControlHeight;
            input.style.marginTop = 0;
            input.style.marginBottom = 0;
            input.style.justifyContent = Justify.Center;
            input.style.alignItems = Align.Center;
        }

        public static string FormatFloat(float value) =>
            value.ToString(CultureInfo.InvariantCulture);

        public static VisualElement CreateVector3Row(
            Vector3 value,
            Action<Vector3> onChange,
            Action<Vector3> onFieldCommit = null)
        {
            onFieldCommit ??= onChange;
            var container = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    width = Length.Percent(100),
                },
            };

            var xField = new FloatField { value = value.x };
            var yField = new FloatField { value = value.y };
            var zField = new FloatField { value = value.z };

            Vector3 Read() => new(xField.value, yField.value, zField.value);

            void Commit() => onChange(Read());
            void CommitWithUndo() => onFieldCommit(Read());

            container.Add(CreateAxisField("X", xField, Commit, CommitWithUndo));
            container.Add(CreateAxisField("Y", yField, Commit, CommitWithUndo));
            container.Add(CreateAxisField("Z", zField, Commit, CommitWithUndo));
            return container;
        }

        private static VisualElement CreateAxisField(
            string axis,
            FloatField field,
            Action onChange,
            Action onCommit)
        {
            var row = new VisualElement
            {
                style =
                {
                    flexDirection = FlexDirection.Row,
                    alignItems = Align.Center,
                    flexGrow = 1,
                    flexShrink = 1,
                    minWidth = 72,
                    marginRight = 4,
                },
            };
            row.Add(new Label(axis)
            {
                style =
                {
                    width = 12,
                    minWidth = 12,
                    color = new Color(0.65f, 0.65f, 0.65f),
                    fontSize = 10,
                    unityTextAlign = TextAnchor.MiddleLeft,
                },
            });
            ConfigureCompactNumericField(field);
            field.style.width = Length.Percent(100);
            field.style.minWidth = 48;
            field.style.borderBottomWidth = 2;
            field.style.borderBottomColor = axis switch
            {
                "X" => new Color(0.82f, 0.32f, 0.32f),
                "Y" => new Color(0.38f, 0.78f, 0.38f),
                _ => new Color(0.38f, 0.55f, 0.9f),
            };
            field.RegisterValueChangedCallback(_ => onChange());
            field.RegisterCallback<FocusOutEvent>(_ => onCommit());
            row.Add(field);
            return row;
        }
    }
}
