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
            bool readOnly = false)
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

                row.Add(slider);
                row.Add(intField);
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

                row.Add(slider);
                row.Add(floatField);
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

        private static void ConfigureCompactNumericField<T>(BaseField<T> field)
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
    }
}
