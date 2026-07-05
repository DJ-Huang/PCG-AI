using System;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;
using DJTechRuntime.PCG;

namespace DJTechEditor.PCG.Graph
{
    public abstract class PcgGraphNodeBase : Node
    {
        public string NodeId { get; private set; }
        public abstract string NodeType { get; }

        protected Port InputPort { get; set; }
        protected Port OutputPort { get; set; }

        protected PcgGraphNodeBase()
        {
            style.minWidth = 200;
        }

        public void Initialize(string id, Vector2 position)
        {
            NodeId = id;
            expanded = true;
            SetPosition(new Rect(position, Vector2.zero));
            BuildPorts();
            RefreshExpandedState();
            RefreshPorts();
        }

        public Port GetInputPort() => InputPort;
        public Port GetOutputPort() => OutputPort;

        public virtual Port FindInputPort(string handle = "in") => InputPort;
        public virtual Port FindOutputPort(string handle = "out") => OutputPort;

        protected virtual void BuildPorts()
        {
            if (NodeType != PcgNodeTypes.ParseConfig)
            {
                InputPort = CreatePort(Direction.Input, "in");
                inputContainer.Add(InputPort);
            }

            if (NodeType != PcgNodeTypes.PlaceInScene)
            {
                OutputPort = CreatePort(Direction.Output, "out");
                outputContainer.Add(OutputPort);
            }
        }

        protected Port CreatePort(Direction direction, string portName)
        {
            var capacity = direction == Direction.Input ? Port.Capacity.Single : Port.Capacity.Single;
            var port = InstantiatePort(Orientation.Horizontal, direction, capacity, typeof(float));
            port.portName = portName;
            return port;
        }

        protected static void AddField<T>(VisualElement parent, string label, T initialValue, Action<T> onChanged)
            where T : struct
        {
            if (typeof(T) == typeof(int))
            {
                var field = new IntegerField(label) { value = Convert.ToInt32(initialValue) };
                field.RegisterValueChangedCallback(evt => onChanged((T)(object)evt.newValue));
                parent.Add(field);
                return;
            }

            if (typeof(T) == typeof(float))
            {
                var field = new FloatField(label) { value = Convert.ToSingle(initialValue) };
                field.RegisterValueChangedCallback(evt => onChanged((T)(object)evt.newValue));
                parent.Add(field);
            }
        }

        protected static void AddTextField(VisualElement parent, string label, string initialValue, Action<string> onChanged)
        {
            var field = new TextField(label) { value = initialValue ?? "" };
            field.RegisterValueChangedCallback(evt => onChanged(evt.newValue));
            parent.Add(field);
        }

        public abstract void ApplyData(PcgNodeData data);
        public abstract PcgNodeData CollectData();
    }
}
